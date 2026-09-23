#include "linux_config.hpp"

#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace studio_remote::linux
{
namespace
{
using json = nlohmann::json;

std::filesystem::path environment_path(const char* name)
{
  const auto* value = std::getenv(name);
  return value && *value ? std::filesystem::path(value) : std::filesystem::path{};
}

std::size_t utf8_code_points(std::string_view text)
{
  std::size_t count = 0;
  for (std::size_t offset = 0; offset < text.size(); ++count)
  {
    const auto first = static_cast<unsigned char>(text[offset]);
    const std::size_t width = first < 0x80             ? 1
                              : (first & 0xe0) == 0xc0 ? 2
                              : (first & 0xf0) == 0xe0 ? 3
                              : (first & 0xf8) == 0xf0 ? 4
                                                       : 0;
    if (!width || offset + width > text.size())
      throw std::invalid_argument("Gateway name is not valid UTF-8");
    for (std::size_t index = 1; index < width; ++index)
      if ((static_cast<unsigned char>(text[offset + index]) & 0xc0) != 0x80)
        throw std::invalid_argument("Gateway name is not valid UTF-8");
    offset += width;
  }
  return count;
}

std::uint16_t required_port(const json& data, std::string_view key, std::uint16_t fallback)
{
  const auto value = data.value(std::string(key), static_cast<int>(fallback));
  if (value < 1 || value > 65'535)
    throw std::invalid_argument(std::string(key) + " is out of range");
  return static_cast<std::uint16_t>(value);
}

void restrict_directory(const std::filesystem::path& directory)
{
  std::filesystem::create_directories(directory);
  std::filesystem::permissions(
      directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
}
} // namespace

std::filesystem::path default_data_directory()
{
  auto state_home = environment_path("XDG_STATE_HOME");
  if (state_home.empty())
  {
    const auto home = environment_path("HOME");
    if (home.empty())
      throw std::runtime_error("Neither XDG_STATE_HOME nor HOME is available");
    state_home = home / ".local" / "state";
  }
  return state_home / "panelive-companion";
}

ConfigStore::ConfigStore(std::filesystem::path directory)
    : directory_(directory.empty() ? default_data_directory() : std::move(directory))
{
}

GatewayConfig ConfigStore::load()
{
  restrict_directory(directory_);
  GatewayConfig result;
  result.data_directory = directory_;
  const auto path = directory_ / "config.json";
  if (std::filesystem::exists(path))
  {
    std::ifstream stream(path);
    if (!stream)
      throw std::runtime_error("Could not open Linux Companion configuration");
    const auto data = json::parse(stream);
    result.name = data.value("name", result.name);
    result.instance_id = data.value("instanceId", "");
    result.instance_revision = data.value("instanceRevision", 1ULL);
    result.port = required_port(data, "port", 7443);
    result.pairing_address = data.value("pairingAddress", "");
    result.obs_host = data.value("obsHost", "127.0.0.1");
    result.obs_port = required_port(data, "obsPort", 4455);
    result.obs_connection_configured =
        data.value("obsConnectionConfigured", data.contains("obsHost"));
    result.default_pairing_role = parse_role(data.value("defaultPairingRole", "viewer"));
    result.allow_non_private_interfaces = data.value("allowNonPrivateInterfaces", false);
    result.enforce_single_operator_client_mode =
        data.value("enforceSingleOperatorClientMode", true);
    result.start_at_login = data.value("startAtLogin", false);
    result.management_interface_mode =
        management_interface_mode_or_default(data.value("managementInterfaceMode", "basic"));
    result.automatic_audio_headroom = data.value("automaticAudioHeadroom", true);
    result.audio_headroom_per_additional_source_db =
        data.value("audioHeadroomPerAdditionalSourceDb", 2U);
    if (result.audio_headroom_per_additional_source_db < 1 ||
        result.audio_headroom_per_additional_source_db > 4)
      result.audio_headroom_per_additional_source_db = 2;
    result.audio_source_audition_bus = data.value("audioSourceAuditionBus", false);
  }
  if (result.instance_id.empty())
    result.instance_id = crypto::uuid_v4();
  result.certificate =
      crypto::ensure_self_signed_certificate(directory_ / "tls", result.instance_id);
  return result;
}

void ConfigStore::save(const GatewayConfig& config) const
{
  auto trimmed = config.name;
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
    trimmed.erase(trimmed.begin());
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
    trimmed.pop_back();
  if (trimmed.empty() || utf8_code_points(trimmed) > 63)
    throw std::invalid_argument("Gateway name must contain 1-63 Unicode characters");
  if (config.instance_id.empty())
    throw std::invalid_argument("Gateway instance identifier is required");

  const json data = {
      {"name", trimmed},
      {"instanceId", config.instance_id},
      {"instanceRevision", config.instance_revision},
      {"port", config.port},
      {"pairingAddress", config.pairing_address},
      {"obsHost", config.obs_host},
      {"obsPort", config.obs_port},
      {"obsConnectionConfigured", config.obs_connection_configured},
      {"defaultPairingRole", role_name(config.default_pairing_role)},
      {"allowNonPrivateInterfaces", config.allow_non_private_interfaces},
      {"enforceSingleOperatorClientMode", config.enforce_single_operator_client_mode},
      {"startAtLogin", config.start_at_login},
      {"managementInterfaceMode", management_interface_mode_name(config.management_interface_mode)},
      {"automaticAudioHeadroom", config.automatic_audio_headroom},
      {"audioHeadroomPerAdditionalSourceDb", config.audio_headroom_per_additional_source_db},
      {"audioSourceAuditionBus", config.audio_source_audition_bus}};
  const auto temporary = directory_ / "config.json.tmp";
  const auto destination = directory_ / "config.json";
  {
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream)
      throw std::runtime_error("Could not create temporary Linux Companion configuration");
    stream << data.dump(2) << '\n';
    stream.flush();
    if (!stream)
      throw std::runtime_error("Could not write Linux Companion configuration");
  }
  std::filesystem::permissions(temporary,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
  const auto descriptor = ::open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
  if (descriptor < 0 || ::fsync(descriptor) != 0)
  {
    if (descriptor >= 0)
      ::close(descriptor);
    throw std::runtime_error("Could not synchronize Linux Companion configuration");
  }
  ::close(descriptor);
  std::filesystem::rename(temporary, destination);
}

const std::filesystem::path& ConfigStore::directory() const noexcept
{
  return directory_;
}
} // namespace studio_remote::linux
