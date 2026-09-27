#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace studio_remote::macos::installer
{
struct InstallationRecord
{
  std::filesystem::path install_directory;
  std::filesystem::path obs_directory;
  std::string installed_providers = "obs,streamlabs,companion-teleprompter";
};

/// The replacement is visible. The installer must handle uncertain durability as a committed
/// result.
class InstallationRecordCommitError final : public std::system_error
{
public:
  explicit InstallationRecordCommitError(int error)
      : std::system_error(error,
                          std::generic_category(),
                          "Installation record saved but directory synchronization failed")
  {
  }
};

/// Called after successful payload placement, with the selected original-user install locations.
/// Writes current-user metadata only; does not install payloads, change runtime settings or acquire
/// rights.
void write_installation_record(const InstallationRecord& record);

/// Explicit native file boundary for staged records and isolated verification. Parent must exist.
/// Before replacement, failure preserves the old file; after replacement, only CommitError is
/// thrown.
void write_installation_record(const InstallationRecord& record,
                               const std::filesystem::path& record_file);
} // namespace studio_remote::macos::installer
