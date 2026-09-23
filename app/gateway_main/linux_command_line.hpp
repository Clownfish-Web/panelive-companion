#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <studio_remote/common/types.hpp>

namespace studio_remote::linux
{
/// Typed result of the Linux companion command line. The composition root acts on the flags that
/// end the process before the runtime starts (help, version, sign-in startup) in that order.
struct CommandLine
{
  std::filesystem::path data_directory;
  std::optional<std::string> name;
  std::optional<std::string> obs_host;
  std::optional<std::uint16_t> obs_port;
  std::optional<std::uint16_t> gateway_port;
  std::optional<std::string> obs_password;
  std::optional<Role> pairing_role;
  std::optional<bool> set_sign_in_startup;
  bool help = false;
  bool version = false;
  bool open_management = false;
};

/// Throws std::invalid_argument with an operator-readable message for an unknown option, a
/// missing value, an out-of-range port, or a provider other than OBS. An unknown pairing role
/// degrades to viewer through the shared parse_role helper.
CommandLine parse_command_line(int argc, char** argv);

void print_help(std::ostream& out);
} // namespace studio_remote::linux
