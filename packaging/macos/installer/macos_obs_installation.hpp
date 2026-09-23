#pragma once

#include <filesystem>
#include <functional>

namespace studio_remote::macos::installer
{
/// Native equivalents of the two registry queries and final default in Inno's DetectObsDirectory.
/// Queries are lazy: an accepted earlier candidate prevents subsequent lookups.
struct ObsInstallationSources
{
  std::function<std::filesystem::path()> companion_record;
  std::function<std::filesystem::path()> obs_registration;
  std::filesystem::path default_directory;
};

bool is_obs_directory(const std::filesystem::path& directory);
std::filesystem::path detect_obs_directory(const std::filesystem::path& explicit_directory,
                                           const ObsInstallationSources& sources);

/// Installer composition entry point. Uses machine metadata, LaunchServices and the native default.
std::filesystem::path detect_obs_directory(const std::filesystem::path& explicit_directory = {});
} // namespace studio_remote::macos::installer
