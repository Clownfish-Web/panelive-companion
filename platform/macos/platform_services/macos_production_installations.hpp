#pragma once

#include <filesystem>

namespace studio_remote::macos
{
/// Shared application placement and current-user metadata, separate from runtime configuration.
std::filesystem::path companion_installation_directory();
std::filesystem::path installation_record_file();

/// Raw installer-selected path. Validate this before display normalization or symlink resolution.
std::filesystem::path recorded_obs_directory(const std::filesystem::path& record_file);

/// Reports the OBS location recorded by the main Companion installer, including stale paths.
/// This is display metadata only; runtime OBS transport and helper IPC do not use it.
std::filesystem::path installed_obs_directory();

/// Native record-file boundary, also used to verify staged installer records in isolation.
/// Missing, unreadable, malformed or empty records produce an empty path, as on Windows.
std::filesystem::path installed_obs_directory(const std::filesystem::path& record_file);
} // namespace studio_remote::macos
