#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace studio_remote::macos
{
inline constexpr std::size_t maximum_configuration_bytes = 1024 * 1024;

/// renameat succeeded; the selected file is visible, but crash durability is not confirmed.
class ConfigurationCommitError final : public std::system_error
{
public:
  explicit ConfigurationCommitError(int error)
      : std::system_error(error,
                          std::generic_category(),
                          "Configuration saved but directory synchronization failed")
  {
  }
};

std::optional<std::string> read_configuration_file(const std::filesystem::path& directory);
void write_configuration_file(const std::filesystem::path& directory, std::string_view contents);
} // namespace studio_remote::macos
