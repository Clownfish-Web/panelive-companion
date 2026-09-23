#include "macos_sign_in_startup.hpp"

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

#include <stdexcept>
#include <string>

namespace studio_remote::macos
{
namespace
{
bool canonical_application_bundle()
{
  NSBundle* bundle = NSBundle.mainBundle;
  NSString* identity = @"panelive-companion";
  return [bundle.bundleURL.pathExtension isEqualToString:@"app"] &&
         [bundle.bundleIdentifier isEqualToString:identity] &&
         [bundle.executableURL.lastPathComponent isEqualToString:identity];
}

SignInStartupStatus service_status(SMAppService* service)
{
  switch (service.status)
  {
  case SMAppServiceStatusNotRegistered:
    return SignInStartupStatus::not_registered;
  case SMAppServiceStatusEnabled:
    return SignInStartupStatus::enabled;
  case SMAppServiceStatusRequiresApproval:
    return SignInStartupStatus::requires_approval;
  case SMAppServiceStatusNotFound:
    return SignInStartupStatus::not_found;
  }
  throw std::runtime_error("Unknown macOS sign-in startup service status");
}

void require_success(BOOL success, NSError* error, const char* operation)
{
  if (success)
    return;
  // Keep arbitrary NSError userInfo out of diagnostics. The native code remains actionable.
  const auto code = error ? static_cast<long long>(error.code) : 0LL;
  throw std::runtime_error(std::string(operation) + " (ServiceManagement error " +
                           std::to_string(code) + ")");
}
} // namespace

SignInStartupStatus MacosSignInStartup::status() const
{
  @autoreleasepool
  {
    if (!canonical_application_bundle())
      return SignInStartupStatus::unavailable_bundle;
    return service_status(SMAppService.mainAppService);
  }
}

bool MacosSignInStartup::enabled() const
{
  return status() == SignInStartupStatus::enabled;
}

void MacosSignInStartup::set_enabled(bool enabled) const
{
  @autoreleasepool
  {
    const auto operation = sign_in_startup_operation(status(), enabled);
    if (operation == SignInStartupOperation::none)
      return;
    SMAppService* service = SMAppService.mainAppService;
    NSError* error = nil;
    if (operation == SignInStartupOperation::register_app)
    {
      const BOOL success = [service registerAndReturnError:&error];
      // Registration may succeed while execution still requires consent in System Settings.
      if (service_status(service) == SignInStartupStatus::requires_approval)
        static_cast<void>(sign_in_startup_operation(SignInStartupStatus::requires_approval, true));
      require_success(success, error, "Could not enable Companion sign-in startup");
      if (service_status(service) != SignInStartupStatus::enabled)
        throw std::runtime_error("Companion sign-in startup registration is not enabled");
    }
    else
    {
      const BOOL success = [service unregisterAndReturnError:&error];
      require_success(success, error, "Could not disable Companion sign-in startup");
      if (service_status(service) != SignInStartupStatus::not_registered)
        throw std::runtime_error("Companion sign-in startup registration is still present");
    }
  }
}
} // namespace studio_remote::macos
