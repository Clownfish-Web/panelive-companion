#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>

namespace studio_remote::macos
{
enum class InstallerOperation
{
  none,
  initialize_settings,
  detect_obs,
  record_installation
};

/// Startup inputs only. Parsing performs no filesystem, credential, native UI, or network work.
/// Passwords are deliberately absent: the owner reads protected stdin only after early exits
/// and instance ownership have been resolved.
struct CommandLine
{
  std::filesystem::path data_directory;
  std::optional<std::string> name;
  std::optional<std::string> providers;
  std::optional<std::string> obs_host;
  std::optional<std::uint16_t> obs_port;
  std::optional<std::uint16_t> gateway_port;
  std::optional<bool> set_sign_in_startup;
  bool help = false;
  bool version = false;
  bool open_management = false;
  bool read_obs_password = false;
  bool read_streamlabs_token = false;
  std::optional<std::uint16_t> streamlabs_port;
  InstallerOperation installer_operation = InstallerOperation::none;
  std::filesystem::path obs_installation;
};

CommandLine parse_command_line(int argc, char** argv);
void print_help(std::ostream& out);
} // namespace studio_remote::macos
