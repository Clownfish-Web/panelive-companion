#include "macos_obs_installation.hpp"

#include "../../../platform/macos/platform_services/macos_production_installations.hpp"

#import <AppKit/AppKit.h>

namespace studio_remote::macos::installer
{
bool is_obs_directory(const std::filesystem::path& directory)
{
  if (directory.empty())
    return false;
  std::error_code error;
  return std::filesystem::is_regular_file(directory / "Contents/MacOS/OBS", error) &&
         std::filesystem::is_regular_file(directory / "Contents/Frameworks/libobs.framework/libobs",
                                          error);
}

std::filesystem::path detect_obs_directory(const std::filesystem::path& explicit_directory,
                                           const ObsInstallationSources& sources)
{
  if (is_obs_directory(explicit_directory))
    return explicit_directory;
  if (sources.companion_record)
  {
    auto candidate = sources.companion_record();
    if (is_obs_directory(candidate))
      return candidate;
  }
  if (sources.obs_registration)
  {
    auto candidate = sources.obs_registration();
    if (is_obs_directory(candidate))
      return candidate;
  }
  return is_obs_directory(sources.default_directory) ? sources.default_directory
                                                     : std::filesystem::path{};
}

std::filesystem::path detect_obs_directory(const std::filesystem::path& explicit_directory)
{
  return detect_obs_directory(
      explicit_directory,
      {[] { return recorded_obs_directory(installation_record_file()); },
       []() -> std::filesystem::path
       {
         @autoreleasepool
         {
           NSURL* application = [NSWorkspace.sharedWorkspace
               URLForApplicationWithBundleIdentifier:@"com.obsproject.obs-studio"];
           const char* path = application.fileSystemRepresentation;
           return path ? std::filesystem::path(path) : std::filesystem::path{};
         }
       },
       "/Applications/OBS.app"});
}
} // namespace studio_remote::macos::installer
