#include "macos_tray_icon.hpp"

#include "companion_tray_artwork.hpp"

#import <AppKit/AppKit.h>

#include <stdexcept>
#include <syslog.h>
#include <utility>

using studio_remote::ManagementInterfaceMode;
using studio_remote::macos::MacosTrayActions;

namespace
{
NSImage* load_icon()
{
  using namespace studio_remote::macos::artwork;
  NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(20, 20)];
  for (NSData* bytes in @[
         [NSData dataWithBytes:companion_20 length:sizeof(companion_20)],
         [NSData dataWithBytes:companion_40 length:sizeof(companion_40)]
       ])
  {
    NSBitmapImageRep* representation = [NSBitmapImageRep imageRepWithData:bytes];
    if (!representation)
      throw std::runtime_error("Could not load the Companion tray icon");
    representation.size = image.size;
    [image addRepresentation:representation];
  }
  [image setTemplate:NO];
  image.accessibilityDescription = @"PaneLive Companion";
  return image;
}

NSMenuItem* menu_action(NSString* title, SEL selector, id target)
{
  NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:selector keyEquivalent:@""];
  item.target = target;
  return item;
}
} // namespace

@interface PLCompanionTrayTarget : NSObject
- (instancetype)initWithActions:(MacosTrayActions)actions;
- (void)invalidate;
- (void)selectIcon:(NSStatusBarButton*)button;
- (NSMenu*)menu;
- (void)openManagement:(id)sender;
- (void)pairClient:(id)sender;
- (void)selectBasic:(id)sender;
- (void)selectAdvanced:(id)sender;
- (void)shutdown:(id)sender;
@end

@implementation PLCompanionTrayTarget
{
  MacosTrayActions _actions;
}
- (instancetype)initWithActions:(MacosTrayActions)actions
{
  self = [super init];
  if (self)
    _actions = std::move(actions);
  return self;
}
- (void)invalidate
{
  _actions = {};
}
- (void)invoke:(const std::function<void()>&)action
{
  try
  {
    if (action)
      action();
  }
  catch (...)
  {
    syslog(LOG_ERR, "Companion tray action failed");
  }
}
- (void)openManagement:(id)sender
{
  static_cast<void>(sender);
  [self invoke:_actions.open_management];
}
- (void)pairClient:(id)sender
{
  static_cast<void>(sender);
  [self invoke:_actions.pair_client];
}
- (void)selectBasic:(id)sender
{
  static_cast<void>(sender);
  [self invoke:[self]
        {
          if (_actions.set_interface_mode)
            _actions.set_interface_mode(ManagementInterfaceMode::basic);
        }];
}
- (void)selectAdvanced:(id)sender
{
  static_cast<void>(sender);
  [self invoke:[self]
        {
          if (_actions.set_interface_mode)
            _actions.set_interface_mode(ManagementInterfaceMode::advanced);
        }];
}
- (void)shutdown:(id)sender
{
  static_cast<void>(sender);
  [self invoke:_actions.shutdown];
}
- (NSMenu*)menu
{
  if (!_actions.interface_mode)
    return nil;
  try
  {
    NSMenu* menu = [NSMenu new];
    menu.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    [menu addItem:menu_action(@"Open PaneLive Companion", @selector(openManagement:), self)];
    [menu addItem:menu_action(@"Pair new client", @selector(pairClient:), self)];
    [menu addItem:NSMenuItem.separatorItem];
    const auto mode = _actions.interface_mode();
    NSMenuItem* basic = menu_action(@"Basic interface", @selector(selectBasic:), self);
    basic.state =
        mode == ManagementInterfaceMode::basic ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:basic];
    NSMenuItem* advanced = menu_action(@"Advanced interface", @selector(selectAdvanced:), self);
    advanced.state =
        mode == ManagementInterfaceMode::advanced ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:advanced];
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItem:menu_action(@"Shutdown Companion", @selector(shutdown:), self)];
    return menu;
  }
  catch (...)
  {
    syslog(LOG_ERR, "Could not create the Companion tray menu");
    return nil;
  }
}
- (void)selectIcon:(NSStatusBarButton*)button
{
  const auto event = NSApp.currentEvent;
  if (event.type == NSEventTypeRightMouseUp ||
      (event.modifierFlags & NSEventModifierFlagControl) != 0)
    [[self menu] popUpMenuPositioningItem:nil
                               atLocation:NSMakePoint(0, NSMinY(button.bounds))
                                   inView:button];
  else
    [self openManagement:button];
}
@end

namespace studio_remote::macos
{
class MacosTrayIcon::Impl
{
public:
  explicit Impl(MacosTrayActions actions)
  {
    if (!NSThread.isMainThread)
      throw std::logic_error("Companion tray must be constructed on the main thread");
    NSImage* image = load_icon();
    target_ = [[PLCompanionTrayTarget alloc] initWithActions:std::move(actions)];
    item_ = [NSStatusBar.systemStatusBar statusItemWithLength:NSSquareStatusItemLength];
    item_.button.image = image;
    item_.button.toolTip = @"PaneLive Companion";
    item_.button.accessibilityLabel = @"PaneLive Companion";
    item_.button.accessibilityIdentifier = @"panelive-companion-tray";
    item_.button.target = target_;
    item_.button.action = @selector(selectIcon:);
    [item_.button sendActionOn:NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
  }
  ~Impl()
  {
    [target_ invalidate];
    item_.button.target = nil;
    item_.button.action = nullptr;
    [NSStatusBar.systemStatusBar removeStatusItem:item_];
  }

private:
  PLCompanionTrayTarget* target_;
  NSStatusItem* item_;
};

MacosTrayIcon::MacosTrayIcon(MacosTrayActions actions)
    : impl_(std::make_unique<Impl>(std::move(actions)))
{
}

MacosTrayIcon::~MacosTrayIcon() = default;
} // namespace studio_remote::macos
