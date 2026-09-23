#include "macos_application_services.hpp"
#include "macos_installer_operations.hpp"
#include "platform_companion_application.hpp"

#include "../../platform/macos/credentials/macos_keychain_interaction.hpp"
#include "../../platform/macos/platform_services/macos_single_instance.hpp"
#include "../../ui/macos/macos_management_ui.hpp"
#include "../../ui/macos/macos_startup_authorization.hpp"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <future>
#include <iostream>
#include <syslog.h>

namespace
{
void stop_application_loop()
{
  [NSApp stop:nil];
  [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                      location:NSZeroPoint
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:0
                                       context:nil
                                       subtype:0
                                         data1:0
                                         data2:0]
           atStart:NO];
}
} // namespace

@interface PLCompanionDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic) studio_remote::macos::MacosManagementUi* management;
@property(nonatomic, strong) NSTimer* timer;
@property(nonatomic) studio_remote::macos::MacosShutdownSignal* shutdown;
@property(nonatomic) studio_remote::macos::MacosSingleInstance* instance;
@property(nonatomic) BOOL showInitially;
@property(nonatomic) BOOL failed;
- (void)openManagement:(id)sender;
- (void)requestShutdown:(id)sender;
- (void)shutdownFromMenu;
- (void)pollLifecycle;
@end

@implementation PLCompanionDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
  static_cast<void>(notification);
  if (self.showInitially)
    [self openManagement:nil];
}
- (BOOL)applicationShouldHandleReopen:(NSApplication*)application hasVisibleWindows:(BOOL)visible
{
  static_cast<void>(application);
  static_cast<void>(visible);
  [self openManagement:nil];
  return YES;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)application
{
  static_cast<void>(application);
  return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)application
{
  static_cast<void>(application);
  [self requestShutdown:nil];
  return NSTerminateCancel;
}
- (void)openManagement:(id)sender
{
  static_cast<void>(sender);
  if (self.management)
    self.management->show_management();
}
- (void)shutdownFromMenu
{
  if (self.management)
    self.management->shutdown();
}
- (void)requestShutdown:(id)sender
{
  static_cast<void>(sender);
  try
  {
    self.shutdown->request_stop();
  }
  catch (const std::exception&)
  {
    syslog(LOG_ERR, "Companion could not signal shutdown");
    self.failed = YES;
    stop_application_loop();
  }
}
- (void)pollLifecycle
{
  try
  {
    if (self.shutdown->wait_for(std::chrono::milliseconds::zero()))
      stop_application_loop();
    else if (self.instance->consume_management_request())
      [self openManagement:nil];
  }
  catch (const std::exception&)
  {
    syslog(LOG_ERR, "Companion lifecycle event failed");
    self.failed = YES;
    stop_application_loop();
  }
}
@end

namespace studio_remote::macos
{
namespace
{
// The UI relinquishes its worker references before this owner joins/destroys blocking services.
class ApplicationServiceOwner final
{
public:
  ApplicationServiceOwner(const CommandLine& command_line,
                          MacosShutdownSignal& shutdown,
                          EnduranceProcessLog& process_log)
  {
    std::promise<std::unique_ptr<MacosApplicationServices>> result;
    auto ready = result.get_future();
    std::jthread bootstrap(
        [&](std::stop_token cancellation)
        {
          try
          {
            result.set_value(std::make_unique<MacosApplicationServices>(
                command_line, shutdown, cancellation, process_log));
          }
          catch (...)
          {
            result.set_exception(std::current_exception());
          }
        });
    while (ready.wait_for(std::chrono::milliseconds::zero()) != std::future_status::ready)
      if (shutdown.wait_for(std::chrono::milliseconds(50)))
      {
        bootstrap.request_stop();
        // The native stop resource is latched. Wait on completion after cancellation instead
        // of spinning on that already-ready resource while a native operation unwinds.
        ready.wait();
      }
    services = ready.get();
  }
  ~ApplicationServiceOwner()
  {
    if (services)
    {
      std::jthread teardown([owned = std::move(services)]() mutable { owned.reset(); });
    }
  }
  void finish()
  {
    std::promise<void> completion;
    auto completed = completion.get_future();
    std::jthread teardown(
        [&completion, owned = std::move(services)]() mutable
        {
          try
          {
            owned->prepare_normal_shutdown();
            owned.reset();
            completion.set_value();
          }
          catch (...)
          {
            owned.reset();
            completion.set_exception(std::current_exception());
          }
        });
    completed.get();
  }
  std::unique_ptr<MacosApplicationServices> services;
};

NSMenuItem* menu_action(NSString* title, SEL action, NSString* key, id target)
{
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
  item.target = target;
  return item;
}

void install_application_menu(PLCompanionDelegate* delegate)
{
  NSMenu* menu = [NSMenu new];
  menu.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  [menu addItem:menu_action(@"Open PaneLive Companion", @selector(openManagement:), @"", delegate)];
  [menu addItem:NSMenuItem.separatorItem];
  [menu
      addItem:menu_action(@"Quit PaneLive Companion", @selector(shutdownFromMenu), @"q", delegate)];
  NSMenu* main_menu = [NSMenu new];
  NSMenuItem* application_menu = [NSMenuItem new];
  application_menu.submenu = [menu copy];
  [main_menu addItem:application_menu];
  NSApp.mainMenu = main_menu;
}

// Native NSApp registrations must be removed before their borrowed UI and backend targets die.
class ApplicationUiRegistration final
{
public:
  ApplicationUiRegistration(MacosApplicationServices& services, PLCompanionDelegate* delegate)
      : services_(services), delegate_(delegate)
  {
  }
  ~ApplicationUiRegistration()
  {
    [delegate_.timer invalidate];
    delegate_.timer = nil;
    services_.disconnect_client_approval_requests();
    services_.on_pairing_closed({});
    delegate_.management = nullptr;
    NSApp.delegate = nil;
    NSApp.mainMenu = nil;
  }
  ApplicationUiRegistration(const ApplicationUiRegistration&) = delete;
  ApplicationUiRegistration& operator=(const ApplicationUiRegistration&) = delete;

private:
  MacosApplicationServices& services_;
  PLCompanionDelegate* delegate_;
};

int run_management(MacosApplicationServices& services,
                   MacosShutdownSignal& shutdown,
                   MacosSingleInstance& instance)
{
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  PLCompanionDelegate* delegate = [PLCompanionDelegate new];
  delegate.shutdown = &shutdown;
  delegate.instance = &instance;
  delegate.showInitially = services.show_management_on_start();
  auto decisions = services.client_approval_callbacks();
  MacosManagementUi management(services.management(),
                               PROJECT_VERSION,
                               std::move(decisions.decide_role),
                               std::move(decisions.decide_global_setting));
  delegate.management = &management;
  ApplicationUiRegistration registration(services, delegate);
  services.connect_client_approval_requests(management.approval_requesters());
  services.on_pairing_closed(management.pairing_closed_handler());
  install_application_menu(delegate);
  NSApp.delegate = delegate;
  __weak PLCompanionDelegate* weak_delegate = delegate;
  delegate.timer = [NSTimer scheduledTimerWithTimeInterval:0.2
                                                   repeats:YES
                                                     block:^(NSTimer*) {
                                                       [weak_delegate pollLifecycle];
                                                     }];
  [NSApp run];
  return delegate.failed ? 1 : 0;
}

bool graphical_session_available()
{
  std::unique_ptr<const __CFDictionary, decltype(&CFRelease)> session(
      CGSessionCopyCurrentDictionary(), CFRelease);
  if (!session)
    return false;
  const auto on_console = CFDictionaryGetValue(session.get(), kCGSessionOnConsoleKey);
  const bool available = on_console == kCFBooleanTrue;
  return available;
}

int run_owned_application(const CommandLine& command_line)
{
  MacosShutdownSignal shutdown;
  MacosSingleInstance instance;
  if (!instance.owns_instance())
  {
    instance.signal_management_request();
    return 0;
  }
  EnduranceProcessLog process_log;
  bool startup_complete = false;
  try
  {
    MacosKeychainInteraction keychain_interaction;
    int result = 0;
    {
      ApplicationServiceOwner owner(command_line, shutdown, process_log);
      startup_complete = true;
      if (!shutdown.wait_for(std::chrono::milliseconds::zero()))
      {
        if (graphical_session_available())
          result = run_management(*owner.services, shutdown, instance);
        else
          shutdown.wait();
      }
      if (result == 0)
        owner.finish();
    }
    if (result == 0)
      process_log.finish();
    else
      process_log.fail("Companion lifecycle event failed");
    return result;
  }
  catch (const std::exception& error)
  {
    // Startup cancellation is normal. Once services exist, a latched shutdown must not hide
    // an error from management delivery or normal teardown preparation.
    if (!startup_complete && shutdown.wait_for(std::chrono::milliseconds::zero()))
    {
      process_log.finish();
      return 0;
    }
    process_log.fail(error.what());
    throw;
  }
}

int run_with_startup_authorization(CommandLine command_line)
{
  try
  {
    return run_owned_application(command_line);
  }
  catch (const KeychainError& error)
  {
    if (error.failure() != KeychainFailure::access_denied || command_line.open_management ||
        !graphical_session_available())
      throw;
    if (!request_keychain_startup_authorization())
      return 1;
    command_line.open_management = true;
    return run_owned_application(command_line);
  }
}

class MacosCompanionApplication final : public PlatformCompanionApplication
{
public:
  int run(int argc, char** argv) override
  {
    @autoreleasepool
    {
      try
      {
        const auto command_line = parse_command_line(argc, argv);
        if (command_line.help)
        {
          print_help(std::cout);
          return 0;
        }
        if (command_line.version)
        {
          std::cout << PROJECT_VERSION << '\n';
          return 0;
        }
        if (command_line.installer_operation != InstallerOperation::none)
          return run_installer_operation(command_line);
        if (command_line.set_sign_in_startup)
        {
          auto setting =
              std::async(std::launch::async,
                         [&]
                         {
                           if (command_line.providers)
                           {
                             MacosSingleInstance instance;
                             if (!instance.owns_instance())
                               throw std::runtime_error(
                                   "Close Companion before changing login provider selection");
                             ConfigStore store(command_line.data_directory);
                             auto config = store.load();
                             config.providers = *command_line.providers;
                             if (config.name.empty())
                               config.name = "PaneLive Companion";
                             store.save(config);
                             MacosSignInStartup().set_enabled(*command_line.set_sign_in_startup);
                             return;
                           }
                           MacosSignInStartup().set_enabled(*command_line.set_sign_in_startup);
                         });
          setting.get();
          return 0;
        }
        return run_with_startup_authorization(command_line);
      }
      catch (const std::exception& error)
      {
        syslog(LOG_ERR, "Companion startup failed");
        std::cerr << "Gateway failed: " << error.what() << '\n';
        return 1;
      }
    }
  }
};
} // namespace
} // namespace studio_remote::macos

namespace studio_remote
{
std::unique_ptr<PlatformCompanionApplication> create_platform_companion_application()
{
  return std::make_unique<macos::MacosCompanionApplication>();
}
} // namespace studio_remote
