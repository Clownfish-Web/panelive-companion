#include "linux_config.hpp"

#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <glib.h>
#include <limits>
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
  if (text.size() > static_cast<std::size_t>(std::numeric_limits<gssize>::max()) ||
      !g_utf8_validate(text.data(), static_cast<gssize>(text.size()), nullptr))
    throw std::invalid_argument("Gateway name is not valid UTF-8");
  return static_cast<std::size_t>(g_utf8_strlen(text.data(), static_cast<gssize>(text.size())));
}

std::uint64_t
integer(const json& data, std::string_view key, std::uint64_t fallback, std::uint64_t maximum)
{
  const auto found = data.find(key);
  if (found == data.end())
    return fallback;
  if (!found->is_number_integer() ||
      (!found->is_number_unsigned() && found->get<std::int64_t>() < 1))
    throw std::invalid_argument(std::string(key) + " is out of range");
  const auto value = found->get<std::uint64_t>();
  if (value < 1 || value > maximum)
    throw std::invalid_argument(std::string(key) + " is out of range");
  return value;
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
    try
    {
      std::ifstream stream(path);
      if (!stream)
        throw std::runtime_error("Could not open Linux Companion configuration");
      const auto data = json::parse(stream);
      if (!data.is_object())
        throw std::invalid_argument("Configuration must be a JSON object");
      result.name = data.value("name", result.name);
      result.instance_id = data.value("instanceId", "");
      result.instance_revision =
          integer(data, "instanceRevision", 1, std::numeric_limits<std::uint64_t>::max());
      result.port = static_cast<std::uint16_t>(integer(data, "port", 7443, 65'535));
      result.pairing_address = data.value("pairingAddress", "");
      result.obs_host = data.value("obsHost", "127.0.0.1");
      result.obs_port = static_cast<std::uint16_t>(integer(data, "obsPort", 4455, 65'535));
      result.obs_connection_configured = data.value(
          "obsConnectionConfigured", data.contains("obsHost") && data.contains("obsPort"));
      result.default_pairing_role = parse_role(data.value("defaultPairingRole", "viewer"));
      result.allow_non_private_interfaces = data.value("allowNonPrivateInterfaces", false);
      result.enforce_single_operator_client_mode =
          data.value("enforceSingleOperatorClientMode", true);
      result.start_at_login = data.value("startAtLogin", false);
      result.management_interface_mode =
          management_interface_mode_or_default(data.value("managementInterfaceMode", "basic"));
      result.automatic_audio_headroom = data.value("automaticAudioHeadroom", true);
      result.audio_headroom_per_additional_source_db =
          static_cast<std::uint32_t>(integer(data, "audioHeadroomPerAdditionalSourceDb", 2, 4));
      result.audio_source_audition_bus = data.value("audioSourceAuditionBus", false);
    }
    catch (const json::exception&)
    {
      throw std::invalid_argument("Linux configuration contains invalid JSON or field types");
    }
  if (result.instance_id.empty())
    result.instance_id = crypto::uuid_v4();
  result.certificate =
      crypto::ensure_self_signed_certificate(directory_ / "tls", result.instance_id);
  return result;
}

void ConfigStore::save(const GatewayConfig& config) const
{
  if (!config.port || !config.obs_port)
    throw std::invalid_argument("Gateway or OBS port is out of range");
  if (!config.instance_revision)
    throw std::invalid_argument("Gateway instance identity is required");
  if (config.audio_headroom_per_additional_source_db < 1 ||
      config.audio_headroom_per_additional_source_db > 4)
    throw std::invalid_argument("Audio monitor headroom preset is invalid");
  auto trimmed = config.name;
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
    trimmed.erase(trimmed.begin());
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
    trimmed.pop_back();
  if (trimmed.empty() || trimmed.find('\0') != std::string::npos || utf8_code_points(trimmed) > 63)
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
    stream.close();
    if (!stream)
      throw std::runtime_error("Could not close Linux Companion configuration");
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
