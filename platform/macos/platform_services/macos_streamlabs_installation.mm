#include "macos_streamlabs_installation.hpp"

#import <AppKit/AppKit.h>
#import <Security/Security.h>

#include <libproc.h>
#include <memory>
#include <stdexcept>
#include <studio_remote/streamlabs/streamlabs_version.hpp>
#include <sys/proc_info.h>
#include <unistd.h>

namespace studio_remote::macos
{
std::string verified_streamlabs_version()
{
  @autoreleasepool
  {
    NSURL* url =
        [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:@"com.streamlabs.slobs"];
    NSBundle* bundle = url ? [NSBundle bundleWithURL:url] : nil;
    NSString* version = [bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    if (!bundle || ![version isKindOfClass:NSString.class] ||
        !streamlabs::supported_native_desktop_version(version.UTF8String, {1, 21, 6, 0}))
      throw std::runtime_error(
          "A supported Streamlabs Desktop 1.21.6 or newer installation was not found");

    SecStaticCodeRef raw_code = nullptr;
    if (SecStaticCodeCreateWithPath((__bridge CFURLRef)url, kSecCSDefaultFlags, &raw_code) !=
        errSecSuccess)
      throw std::runtime_error("Streamlabs application signature is unavailable");
    const auto code = std::unique_ptr<const __SecCode, decltype(&CFRelease)>(raw_code, CFRelease);
    SecRequirementRef raw_requirement = nullptr;
    const auto status = SecRequirementCreateWithString(
        CFSTR("anchor apple generic and identifier \"com.streamlabs.slobs\" and certificate "
              "leaf[subject.OU] = \"UT675MBB9Q\""),
        kSecCSDefaultFlags,
        &raw_requirement);
    if (status != errSecSuccess)
      throw std::runtime_error("Streamlabs application trust requirement is unavailable");
    const auto requirement =
        std::unique_ptr<__SecRequirement, decltype(&CFRelease)>(raw_requirement, CFRelease);
    if (SecStaticCodeCheckValidity(code.get(), kSecCSCheckAllArchitectures, requirement.get()) !=
        errSecSuccess)
      throw std::runtime_error("Streamlabs application signature is not trusted");

    const auto expected = bundle.executableURL.URLByResolvingSymlinksInPath.path;
    for (NSRunningApplication* running in
         [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.streamlabs.slobs"])
    {
      const auto actual = running.executableURL.URLByResolvingSymlinksInPath.path;
      proc_bsdinfo info{};
      if ([actual isEqualToString:expected] &&
          proc_pidinfo(running.processIdentifier, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) ==
              sizeof(info) &&
          info.pbi_uid == getuid())
        return version.UTF8String;
    }
    throw std::runtime_error(
        "Streamlabs Desktop is not running from the trusted installation for this user");
  }
}
} // namespace studio_remote::macos
