#include "macos_command_line.hpp"
#include "../../platform/macos/platform_services/macos_config.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace studio_remote::macos
{
namespace
{
std::uint16_t parse_port(std::string_view value, std::string_view label)
{
  unsigned int parsed = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec == std::errc::invalid_argument)
    throw std::invalid_argument(std::string(label) + " is not a number");
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0 ||
      parsed > 65'535)
    throw std::invalid_argument(std::string(label) + " is out of range");
  return static_cast<std::uint16_t>(parsed);
}

bool parse_startup(std::string_view value)
{
  if (value == "enabled")
    return true;
  if (value == "disabled")
    return false;
  throw std::invalid_argument("Sign-in startup must be enabled or disabled");
}

void parse_value_option(CommandLine& result, std::string_view option, std::string_view value)
{
  if (option == "--data-directory")
    result.data_directory = value;
  else if (option == "--obs-installation")
    result.obs_installation = value;
  else if (option == "--name")
    result.name = value;
  else if (option == "--obs-host")
    result.obs_host = value;
  else if (option == "--obs-port")
    result.obs_port = parse_port(value, "OBS port");
  else if (option == "--streamlabs-port")
    result.streamlabs_port = parse_port(value, "Streamlabs port");
  else if (option == "--gateway-port")
    result.gateway_port = parse_port(value, "Gateway port");
  else if (option == "--set-sign-in-startup")
    result.set_sign_in_startup = parse_startup(value);
  else if (option == "--providers")
    result.providers = selected_providers(value).normalized;
}

bool takes_value(std::string_view option)
{
  return option == "--data-directory" || option == "--name" || option == "--obs-host" ||
         option == "--obs-port" || option == "--streamlabs-port" || option == "--gateway-port" ||
         option == "--set-sign-in-startup" || option == "--providers" ||
         option == "--obs-installation";
}
} // namespace

CommandLine parse_command_line(int argc, char** argv)
{
  if (argc < 1 || !argv)
    throw std::invalid_argument("Invalid command line");
  CommandLine result;
  for (int index = 1; index < argc; ++index)
  {
    if (!argv[index])
      throw std::invalid_argument("Invalid command line");
    const std::string_view option = argv[index];
    if (option == "--help" || option == "-h")
      result.help = true;
    else if (option == "--version")
      result.version = true;
    else if (option == "--open-management")
      result.open_management = true;
    else if (option == "--obs-password-stdin")
      result.read_obs_password = true;
    else if (option == "--streamlabs-token-stdin")
      result.read_streamlabs_token = true;
    else if (option == "--initialize-installation" || option == "--detect-obs-installation" ||
             option == "--record-installation")
    {
      if (result.installer_operation != InstallerOperation::none)
        throw std::invalid_argument("Select only one installer operation");
      result.installer_operation =
          option == "--initialize-installation"   ? InstallerOperation::initialize_settings
          : option == "--detect-obs-installation" ? InstallerOperation::detect_obs
                                                  : InstallerOperation::record_installation;
    }
    else if (takes_value(option))
    {
      if (index + 1 >= argc || !argv[index + 1] || std::string_view(argv[index + 1]).empty() ||
          std::string_view(argv[index + 1]).starts_with("--"))
        throw std::invalid_argument(std::string(option) + " requires a value");
      parse_value_option(result, option, argv[++index]);
    }
    else if (option == "--obs-password" || option.starts_with("--obs-password="))
      throw std::invalid_argument("Use --obs-password-stdin to supply the OBS password");
    else
      // Unknown arguments may be accidentally supplied secrets. Never echo their contents.
      throw std::invalid_argument("Unknown command-line option; use --help for supported options");
  }
  if (result.read_obs_password && result.read_streamlabs_token)
    throw std::invalid_argument("Import one backend credential at a time");
  if (result.installer_operation != InstallerOperation::none &&
      (result.read_obs_password || result.read_streamlabs_token || result.set_sign_in_startup ||
       result.open_management || result.name || result.providers || result.obs_host ||
       result.obs_port || result.streamlabs_port || result.gateway_port))
    throw std::invalid_argument(
        "Installer operations cannot change runtime settings or credentials");
  if (!result.obs_installation.empty() &&
      result.installer_operation != InstallerOperation::detect_obs &&
      result.installer_operation != InstallerOperation::record_installation)
    throw std::invalid_argument(
        "OBS installation is only valid for installer detection or recording");
  return result;
}

void print_help(std::ostream& out)
{
  out << "Usage: panelive-companion [options]\n"
         "  --data-directory PATH  Override the per-user data directory\n"
         "  --providers IDS        Comma-separated obs,streamlabs,companion-teleprompter "
         "selection\n"
         "  --name NAME            Set the advertised Companion name\n"
         "  --gateway-port PORT    Set the HTTPS/WSS listener port\n"
         "  --obs-host HOST        Set the OBS WebSocket host\n"
         "  --obs-port PORT        Set the OBS WebSocket port\n"
         "  --obs-password-stdin   Read the OBS password from protected standard input\n"
         "  --streamlabs-port PORT Set the local Streamlabs WebSocket port\n"
         "  --streamlabs-token-stdin Import the Streamlabs API token from protected input\n"
         "  --set-sign-in-startup enabled|disabled\n"
         "  --open-management      Show or activate the management window\n"
         "  --initialize-installation Initialize missing per-user settings, then exit\n"
         "  --detect-obs-installation Print the detected OBS application path, then exit\n"
         "  --record-installation   Record installed application locations (installer only)\n"
         "  --obs-installation PATH Explicit OBS application for detection or recording\n"
         "  --version              Print the version\n"
         "  --help, -h             Print this help\n";
}
} // namespace studio_remote::macos
