#include "linux_command_line.hpp"

#include <stdexcept>
#include <string_view>

namespace studio_remote::linux
{
namespace
{
std::uint16_t parse_port(std::string_view value, std::string_view label)
{
  std::size_t consumed = 0;
  int parsed = 0;
  try
  {
    parsed = std::stoi(std::string(value), &consumed);
  }
  catch (const std::logic_error&)
  {
    throw std::invalid_argument(std::string(label) + " is not a number");
  }
  if (consumed != value.size() || parsed < 1 || parsed > 65'535)
    throw std::invalid_argument(std::string(label) + " is out of range");
  return static_cast<std::uint16_t>(parsed);
}

bool parse_sign_in_startup(std::string_view setting)
{
  if (setting == "enabled")
    return true;
  if (setting == "disabled")
    return false;
  throw std::invalid_argument("Sign-in startup must be enabled or disabled");
}
} // namespace

CommandLine parse_command_line(int argc, char** argv)
{
  CommandLine result;
  const auto value = [&](int& index, std::string_view option) -> std::string_view
  {
    if (index + 1 >= argc)
      throw std::invalid_argument(std::string(option) + " requires a value");
    return argv[++index];
  };
  for (int index = 1; index < argc; ++index)
  {
    const std::string_view option = argv[index];
    if (option == "--help" || option == "-h")
      result.help = true;
    else if (option == "--version")
      result.version = true;
    else if (option == "--data-directory")
      result.data_directory = value(index, option);
    else if (option == "--name")
      result.name = value(index, option);
    else if (option == "--obs-host")
      result.obs_host = value(index, option);
    else if (option == "--obs-port")
      result.obs_port = parse_port(value(index, option), "OBS port");
    else if (option == "--gateway-port")
      result.gateway_port = parse_port(value(index, option), "Gateway port");
    else if (option == "--obs-password")
      result.obs_password = value(index, option);
    else if (option == "--pairing-role")
      result.pairing_role = parse_role(value(index, option));
    else if (option == "--set-sign-in-startup")
      result.set_sign_in_startup = parse_sign_in_startup(value(index, option));
    else if (option == "--providers")
    {
      if (value(index, option) != "obs")
        throw std::invalid_argument("The Linux build supports only the OBS provider");
    }
    else if (option == "--open-management")
      result.open_management = true;
    else
      throw std::invalid_argument("Unknown option: " + std::string(option));
  }
  return result;
}

void print_help(std::ostream& out)
{
  out << "Usage: panelive-companion [options]\n"
         "  --data-directory PATH  Override the XDG state directory\n"
         "  --name NAME             Set the advertised Companion name\n"
         "  --gateway-port PORT     Set the HTTPS/WSS listener port\n"
         "  --obs-host HOST         Set the OBS WebSocket host\n"
         "  --obs-port PORT         Set the OBS WebSocket port\n"
         "  --obs-password VALUE    Store the OBS password in Secret Service\n"
         "  --pairing-role ROLE     Print a five-minute pairing payload\n"
         "  --providers obs         Select the production provider (OBS only on Linux)\n"
         "                          PaneLive Teleprompter is always included and started\n"
         "  --set-sign-in-startup enabled|disabled\n"
         "  --open-management       Show the management window, activating a running instance\n"
         "  --version               Print the version\n";
}
} // namespace studio_remote::linux
