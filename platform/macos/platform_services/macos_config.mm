#include "macos_config.hpp"

#include "macos_config_file.hpp"
#include "macos_platform_services.hpp"

#import <Foundation/Foundation.h>

#include <cctype>
#include <limits>
#include <stdexcept>
#include <studio_remote/credentials/credential_store.hpp>
#include <studio_remote/provider/provider_catalog.hpp>
#include <utility>

namespace studio_remote::macos
{
namespace
{
void validate_credential_reference(std::string_view reference, std::string_view key)
{
  if (reference == key)
    return;
  const std::string prefix = std::string(key) + "-";
  if (!reference.starts_with(prefix) || reference.size() != prefix.size() + 36)
    throw std::invalid_argument("Invalid backend credential reference");
  const auto uuid = reference.substr(prefix.size());
  for (std::size_t index = 0; index < uuid.size(); ++index)
  {
    const auto value = uuid[index];
    const bool separator = index == 8 || index == 13 || index == 18 || index == 23;
    if (separator ? value != '-'
                  : !((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')))
      throw std::invalid_argument("Invalid backend credential reference");
  }
}

bool remove_backend_credential(ICredentialStore& credentials, std::string_view reference)
{
  try
  {
    credentials.put(reference, "");
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

void stage_backend_credential(ICredentialStore& credentials,
                              std::string_view reference,
                              std::string_view password)
{
  try
  {
    credentials.put(reference, password);
  }
  catch (const std::exception&)
  {
    if (!remove_backend_credential(credentials, reference))
      throw std::runtime_error(
          "Backend credential staging failed; staged credential cleanup failed");
    throw;
  }
}

std::uint64_t integer(const json& data,
                      const char* key,
                      std::uint64_t fallback,
                      std::uint64_t maximum,
                      const char* error)
{
  const auto found = data.find(key);
  if (found == data.end())
    return fallback;
  if (!found->is_number_integer() ||
      (!found->is_number_unsigned() && found->get<std::int64_t>() < 1))
    throw std::invalid_argument(error);
  const auto value = found->get<std::uint64_t>();
  if (value < 1 || value > maximum)
    throw std::invalid_argument(error);
  return value;
}

std::string trimmed_name(std::string name)
{
  while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
    name.erase(name.begin());
  while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
    name.pop_back();
  NSString* text = [[NSString alloc] initWithBytes:name.data()
                                            length:name.size()
                                          encoding:NSUTF8StringEncoding];
  if (!text)
    throw std::invalid_argument("Gateway name is not valid UTF-8");
  const auto count = [text lengthOfBytesUsingEncoding:NSUTF32LittleEndianStringEncoding] / 4;
  if (count < 1 || count > 63 || name.find('\0') != std::string::npos)
    throw std::invalid_argument("Gateway name must contain 1-63 Unicode characters");
  return name;
}

void decode_settings(const json& data, GatewayConfig& result)
{
  if (!data.is_object())
    throw std::invalid_argument("Configuration must be a JSON object");
  result.providers = selected_providers(data.value("providers", result.providers)).normalized;
  result.name = data.value("name", "");
  result.instance_id = data.value("instanceId", "");
  result.instance_revision = integer(data,
                                     "instanceRevision",
                                     1,
                                     std::numeric_limits<std::uint64_t>::max(),
                                     "Gateway instance revision is out of range");
  result.port = static_cast<std::uint16_t>(
      integer(data, "port", 7443, 65535, "Gateway port is out of range"));
  result.bind_address = data.value("bindAddress", "0.0.0.0");
  result.pairing_address = data.value("pairingAddress", "");
  result.obs_host = data.value("obsHost", "127.0.0.1");
  result.obs_credential_reference = data.value("obsCredentialReference", "obs-password");
  validate_credential_reference(result.obs_credential_reference, "obs-password");
  result.streamlabs_credential_reference =
      data.value("streamlabsCredentialReference", "streamlabs-api-token");
  validate_credential_reference(result.streamlabs_credential_reference, "streamlabs-api-token");
  result.obs_port =
      static_cast<std::uint16_t>(integer(data, "obsPort", 4455, 65535, "OBS port is out of range"));
  result.streamlabs_port = static_cast<std::uint16_t>(
      integer(data, "streamlabsPort", 59650, 65535, "Streamlabs port is out of range"));
  result.obs_connection_configured =
      data.value("obsConnectionConfigured", data.contains("obsHost") && data.contains("obsPort"));
  result.default_pairing_role = parse_role(data.value("defaultPairingRole", "viewer"));
  result.allow_non_private_interfaces = data.value("allowNonPrivateInterfaces", false);
  result.enforce_single_operator_client_mode = data.value("enforceSingleOperatorClientMode", true);
  result.management_interface_mode =
      management_interface_mode_or_default(data.value("managementInterface", "basic"));
  result.audio_monitor_mix.automatic_headroom = data.value("audioMonitorAutomaticHeadroom", true);
  result.audio_monitor_mix.attenuation_per_additional_source_db =
      static_cast<std::uint32_t>(integer(data,
                                         "audioMonitorHeadroomPerAdditionalSourceDb",
                                         default_audio_monitor_headroom_db,
                                         4,
                                         "Audio monitor headroom preset is invalid"));
  result.audio_monitor_mix.activate_audio_source_audition_bus =
      data.value("activateAudioSourceAuditionBus", false);
}

json encode_settings(const GatewayConfig& config)
{
  validate_credential_reference(config.obs_credential_reference, "obs-password");
  validate_credential_reference(config.streamlabs_credential_reference, "streamlabs-api-token");
  if (!config.port)
    throw std::invalid_argument("Gateway port is out of range");
  if (!config.obs_port)
    throw std::invalid_argument("OBS port is out of range");
  if (config.instance_id.empty() || !config.instance_revision)
    throw std::invalid_argument("Gateway instance identity is required");
  if (!valid_audio_monitor_headroom_db(
          config.audio_monitor_mix.attenuation_per_additional_source_db))
    throw std::invalid_argument("Audio monitor headroom preset is invalid");
  return {{"providers", selected_providers(config.providers).normalized},
          {"name", trimmed_name(config.name)},
          {"instanceId", config.instance_id},
          {"instanceRevision", config.instance_revision},
          {"port", config.port},
          {"bindAddress", config.bind_address},
          {"pairingAddress", config.pairing_address},
          {"obsHost", config.obs_host},
          {"obsCredentialReference", config.obs_credential_reference},
          {"obsPort", config.obs_port},
          {"streamlabsPort", config.streamlabs_port},
          {"streamlabsCredentialReference", config.streamlabs_credential_reference},
          {"obsConnectionConfigured", config.obs_connection_configured},
          {"defaultPairingRole", role_name(config.default_pairing_role)},
          {"allowNonPrivateInterfaces", config.allow_non_private_interfaces},
          {"enforceSingleOperatorClientMode", config.enforce_single_operator_client_mode},
          {"managementInterface", management_interface_mode_name(config.management_interface_mode)},
          {"audioMonitorAutomaticHeadroom", config.audio_monitor_mix.automatic_headroom},
          {"audioMonitorHeadroomPerAdditionalSourceDb",
           config.audio_monitor_mix.attenuation_per_additional_source_db},
          {"activateAudioSourceAuditionBus",
           config.audio_monitor_mix.activate_audio_source_audition_bus}};
}
} // namespace

std::string default_providers()
{
  std::string result;
  for (const auto& provider : built_production_providers())
  {
    if (!result.empty())
      result.push_back(',');
    result += provider.key;
  }
  return result;
}

ProviderSelection selected_providers(std::string_view value)
{
  std::vector<std::string_view> built;
  for (const auto& provider : built_production_providers())
    built.push_back(provider.key);
  return parse_provider_selection(value, built);
}

ConfigStore::ConfigStore(std::filesystem::path directory, FileWriter writer)
    : directory_(directory.empty() ? MacosPlatformServices{}.data_directory()
                                   : std::move(directory)),
      writer_(std::move(writer))
{
}

GatewayConfig ConfigStore::load() const
{
  @autoreleasepool
  {
    GatewayConfig result;
    if (const auto contents = read_configuration_file(directory_))
    {
      try
      {
        decode_settings(json::parse(*contents), result);
      }
      catch (const json::exception&)
      {
        // JSON parser errors may include values. Never include local configuration in diagnostics.
        throw std::invalid_argument("macOS configuration contains invalid JSON or field types");
      }
    }
    if (result.instance_id.empty())
      result.instance_id = NSUUID.UUID.UUIDString.lowercaseString.UTF8String;
    return result;
  }
}

void ConfigStore::save(const GatewayConfig& config) const
{
  @autoreleasepool
  {
    writer_(directory_, encode_settings(config).dump(2) + '\n');
  }
}

void ConfigStore::initialize_installation() const
{
  // Retain unknown future settings as well as credentials and identity on upgrades.
  if (read_configuration_file(directory_))
  {
    static_cast<void>(load());
    return;
  }
  auto config = load();
  config.name = "PaneLive Companion";
  save(config);
}

SavedObsConnection
ConfigStore::save_obs_connection(GatewayConfig config,
                                 ICredentialStore& credentials,
                                 const std::optional<std::string>& replacement) const
{
  return save_backend_connection(std::move(config),
                                 credentials,
                                 replacement,
                                 &GatewayConfig::obs_credential_reference,
                                 "obs-password",
                                 "OBS");
}

SavedObsConnection
ConfigStore::save_streamlabs_connection(GatewayConfig config,
                                        ICredentialStore& credentials,
                                        const std::optional<std::string>& replacement) const
{
  return save_backend_connection(std::move(config),
                                 credentials,
                                 replacement,
                                 &GatewayConfig::streamlabs_credential_reference,
                                 "streamlabs-api-token",
                                 "Streamlabs");
}

SavedObsConnection
ConfigStore::save_backend_connection(GatewayConfig config,
                                     ICredentialStore& credentials,
                                     const std::optional<std::string>& replacement,
                                     std::string GatewayConfig::* reference,
                                     std::string_view key,
                                     std::string_view label) const
{
  @autoreleasepool
  {
    validate_credential_reference(config.*reference, key);
    const auto previous = (config.*reference);
    if (replacement)
    {
      (config.*reference) = std::string(key) + "-" + credentials.uuid_v4();
      if ((config.*reference) == previous)
        throw std::runtime_error("Could not allocate a fresh backend credential reference");
    }
    // Complete validation/serialization before either credential or file mutation.
    const auto contents = encode_settings(config).dump(2) + '\n';
    if (contents.size() > maximum_configuration_bytes)
      throw std::invalid_argument("Configuration exceeds the maximum size");
    SavedObsConnection result{std::move(config), {}, {}};
    result.password = replacement ? *replacement : credentials.get(previous);
    if (replacement)
      stage_backend_credential(credentials, (result.config.*reference), result.password);
    try
    {
      writer_(directory_, contents);
    }
    catch (const ConfigurationCommitError&)
    {
      // A crash might recover either file. Keep both credentials and publish the visible one.
      result.warning =
          std::string(label) +
          " settings saved, but durability is unconfirmed; previous credential retained";
      return result;
    }
    catch (const std::exception&)
    {
      if (replacement && !remove_backend_credential(credentials, (result.config.*reference)))
        throw std::runtime_error(std::string(label) +
                                 " settings were not saved; staged credential cleanup failed");
      throw;
    }
    if (replacement && !remove_backend_credential(credentials, previous))
      result.warning =
          std::string(label) + " settings saved, but previous credential cleanup failed";
    return result;
  }
}

const std::filesystem::path& ConfigStore::directory() const noexcept
{
  return directory_;
}
} // namespace studio_remote::macos
