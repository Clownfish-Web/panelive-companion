#include "windows_config.hpp"
#include "../../shared/configuration/configuration_stream.hpp"
#include "windows_obs_connection.hpp"
#include <ShlObj.h>
#include <Windows.h>
#include <cctype>
#include <fstream>
#include <limits>

namespace studio_remote::windows
{
namespace
{
std::filesystem::path default_directory()
{
  PWSTR raw = nullptr;
  if (SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw) != S_OK)
    throw std::runtime_error("Could not resolve LocalAppData");
  std::filesystem::path path(raw);
  CoTaskMemFree(raw);
  return path / "panelive-companion";
}
std::size_t utf8_code_points(std::string_view text)
{
  if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Gateway name must contain 1-63 Unicode characters");
  if (!MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0))
    throw std::invalid_argument("Gateway name is not valid UTF-8");
  std::size_t count = 0;
  // Strict native decoding guarantees each non-continuation byte starts one Unicode scalar.
  for (const unsigned char byte : text)
    if ((byte & 0xc0) != 0x80)
      ++count;
  return count;
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
} // namespace

ConfigStore::ConfigStore(std::filesystem::path directory)
    : directory_(directory.empty() ? default_directory() : std::move(directory))
{
}

GatewayConfig ConfigStore::load()
{
  std::filesystem::create_directories(directory_);
  GatewayConfig result;
  result.data_directory = directory_;
  const auto path = directory_ / "config.json";
  if (std::filesystem::exists(path))
    try
    {
      std::ifstream stream(path);
      json data = json::parse(stream);
      if (!data.is_object())
        throw std::invalid_argument("Configuration must be a JSON object");
      result.name = data.value("name", "");
      result.instance_id = data.value("instanceId", "");
      result.instance_revision = integer(data,
                                         "instanceRevision",
                                         1,
                                         std::numeric_limits<std::uint64_t>::max(),
                                         "Gateway instance revision is out of range");
      const auto gateway_port = integer(data, "port", 7443, 65535, "Gateway port is out of range");
      result.port = static_cast<std::uint16_t>(gateway_port);
      result.bind_address = data.value("bindAddress", "0.0.0.0");
      result.pairing_address = data.value("pairingAddress", "");
      result.obs_host = data.value("obsHost", "127.0.0.1");
      const auto obs_port = integer(data, "obsPort", 4455, 65535, "OBS port is out of range");
      result.obs_port = static_cast<std::uint16_t>(obs_port);
      result.obs_connection_configured = data.value(
          "obsConnectionConfigured", data.contains("obsHost") && data.contains("obsPort"));
      result.vmix_host = data.value("vmixHost", "127.0.0.1");
      const auto vmix_http_port =
          integer(data, "vmixHttpPort", 8088, 65535, "vMix HTTP port is out of range");
      result.vmix_http_port = static_cast<std::uint16_t>(vmix_http_port);
      result.vmix_username = data.value("vmixUsername", "");
      result.vmix_connection_configured = data.value("vmixConnectionConfigured", false);
      result.vmix_streaming_configured = data.value("vmixStreamingConfigured", false);
      const auto vmix_srt_program_output =
          integer(data, "vmixSrtProgramOutput", 1, 4, "vMix SRT outputs are invalid");
      const auto vmix_srt_preview_output =
          integer(data, "vmixSrtPreviewOutput", 2, 4, "vMix SRT outputs are invalid");
      const auto vmix_srt_program_port =
          integer(data, "vmixSrtProgramPort", 10'000, 65'535, "vMix SRT ports are invalid");
      const auto vmix_srt_preview_port =
          integer(data, "vmixSrtPreviewPort", 10'001, 65'535, "vMix SRT ports are invalid");
      const auto vmix_srt_latency_ms =
          integer(data, "vmixSrtLatencyMs", 160, 8'000, "vMix SRT latency is out of range");
      if (vmix_srt_program_output < 1 || vmix_srt_program_output > 4 ||
          vmix_srt_preview_output < 1 || vmix_srt_preview_output > 4 ||
          vmix_srt_program_output == vmix_srt_preview_output)
        throw std::invalid_argument("vMix SRT outputs are invalid");
      if (vmix_srt_program_port < 1 || vmix_srt_program_port > 65'535 ||
          vmix_srt_preview_port < 1 || vmix_srt_preview_port > 65'535 ||
          vmix_srt_program_port == vmix_srt_preview_port ||
          vmix_srt_program_port == vmix_http_port || vmix_srt_preview_port == vmix_http_port)
        throw std::invalid_argument("vMix SRT ports are invalid");
      if (vmix_srt_latency_ms < 20 || vmix_srt_latency_ms > 8'000)
        throw std::invalid_argument("vMix SRT latency is out of range");
      result.vmix_srt_program_output = static_cast<std::uint8_t>(vmix_srt_program_output);
      result.vmix_srt_preview_output = static_cast<std::uint8_t>(vmix_srt_preview_output);
      result.vmix_srt_program_port = static_cast<std::uint16_t>(vmix_srt_program_port);
      result.vmix_srt_preview_port = static_cast<std::uint16_t>(vmix_srt_preview_port);
      result.vmix_srt_latency_ms = static_cast<std::uint32_t>(vmix_srt_latency_ms);
      result.vmix_program_ndi_source = data.value("vmixProgramNdiSource", "");
      result.vmix_preview_ndi_source = data.value("vmixPreviewNdiSource", "");
      result.vmix_installation_directory = data.value("vmixInstallationDirectory", "");
      result.default_pairing_role = parse_role(data.value("defaultPairingRole", "viewer"));
      result.allow_non_private_interfaces = data.value("allowNonPrivateInterfaces", false);
      result.enforce_single_operator_client_mode =
          data.value("enforceSingleOperatorClientMode", true);
      result.management_interface_mode =
          management_interface_mode_or_default(data.value("managementInterface", "basic"));
      result.audio_monitor_mix.automatic_headroom =
          data.value("audioMonitorAutomaticHeadroom", true);
      const auto headroom_db =
          static_cast<std::uint32_t>(integer(data,
                                             "audioMonitorHeadroomPerAdditionalSourceDb",
                                             default_audio_monitor_headroom_db,
                                             4,
                                             "Audio monitor headroom preset is invalid"));
      if (!valid_audio_monitor_headroom_db(headroom_db))
        throw std::invalid_argument("Audio monitor headroom preset is invalid");
      result.audio_monitor_mix.attenuation_per_additional_source_db = headroom_db;
      result.audio_monitor_mix.activate_audio_source_audition_bus =
          data.value("activateAudioSourceAuditionBus", false);
      const auto protected_password = data.value("obsPasswordProtected", "");
      result.obs_password_protected = protected_password;
      if (!protected_password.empty())
      {
        try
        {
          result.obs_password = WindowsCrypto::dpapi_unprotect(protected_password);
        }
        catch (const std::exception&)
        {
          result.obs_password.clear();
        }
      }
      const auto protected_vmix_password = data.value("vmixPasswordProtected", "");
      result.vmix_password_protected = protected_vmix_password;
      if (!protected_vmix_password.empty())
      {
        try
        {
          result.vmix_password = WindowsCrypto::dpapi_unprotect(protected_vmix_password);
        }
        catch (const std::exception&)
        {
          result.vmix_password.clear();
        }
      }
    }
    catch (const json::exception&)
    {
      throw std::invalid_argument("Windows configuration contains invalid JSON or field types");
    }
  if (result.instance_id.empty())
    result.instance_id = WindowsCrypto::uuid_v4();
  result.certificate =
      WindowsCrypto::ensure_self_signed_certificate(directory_ / "tls", result.instance_id);
  return result;
}

void ConfigStore::save(const GatewayConfig& config)
{
  if (!config.port)
    throw std::invalid_argument("Gateway port is out of range");
  if (!config.obs_port)
    throw std::invalid_argument("OBS port is out of range");
  if (config.instance_id.empty() || !config.instance_revision)
    throw std::invalid_argument("Gateway instance identity is required");
  if (!valid_audio_monitor_headroom_db(
          config.audio_monitor_mix.attenuation_per_additional_source_db))
    throw std::invalid_argument("Audio monitor headroom preset is invalid");
  const VmixConnectionSettings srt_settings{config.vmix_host,
                                            config.vmix_http_port,
                                            config.vmix_username,
                                            std::nullopt,
                                            config.vmix_srt_program_output,
                                            config.vmix_srt_preview_output,
                                            config.vmix_srt_program_port,
                                            config.vmix_srt_preview_port,
                                            config.vmix_srt_latency_ms};
  if (!valid_vmix_srt_settings(srt_settings))
    throw std::invalid_argument("vMix SRT outputs and ports are invalid");
  std::string trimmed = config.name;
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
    trimmed.erase(0, 1);
  while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
    trimmed.pop_back();
  if (trimmed.empty() || trimmed.find('\0') != std::string::npos || utf8_code_points(trimmed) > 63)
    throw std::invalid_argument("Gateway name must contain 1-63 Unicode characters");
  json data = {
      {"name", trimmed},
      {"instanceId", config.instance_id},
      {"instanceRevision", config.instance_revision},
      {"port", config.port},
      {"bindAddress", config.bind_address},
      {"pairingAddress", config.pairing_address},
      {"obsHost", config.obs_host},
      {"obsPort", config.obs_port},
      {"obsConnectionConfigured", config.obs_connection_configured},
      {"vmixHost", config.vmix_host},
      {"vmixHttpPort", config.vmix_http_port},
      {"vmixUsername", config.vmix_username},
      {"vmixConnectionConfigured", config.vmix_connection_configured},
      {"vmixStreamingConfigured", config.vmix_streaming_configured},
      {"vmixSrtProgramOutput", config.vmix_srt_program_output},
      {"vmixSrtPreviewOutput", config.vmix_srt_preview_output},
      {"vmixSrtProgramPort", config.vmix_srt_program_port},
      {"vmixSrtPreviewPort", config.vmix_srt_preview_port},
      {"vmixSrtLatencyMs", config.vmix_srt_latency_ms},
      {"vmixProgramNdiSource", config.vmix_program_ndi_source},
      {"vmixPreviewNdiSource", config.vmix_preview_ndi_source},
      {"vmixInstallationDirectory", config.vmix_installation_directory},
      {"defaultPairingRole", role_name(config.default_pairing_role)},
      {"allowNonPrivateInterfaces", config.allow_non_private_interfaces},
      {"enforceSingleOperatorClientMode", config.enforce_single_operator_client_mode},
      {"managementInterface", management_interface_mode_name(config.management_interface_mode)},
      {"audioMonitorAutomaticHeadroom", config.audio_monitor_mix.automatic_headroom},
      {"audioMonitorHeadroomPerAdditionalSourceDb",
       config.audio_monitor_mix.attenuation_per_additional_source_db},
      {"activateAudioSourceAuditionBus",
       config.audio_monitor_mix.activate_audio_source_audition_bus},
      {"obsPasswordProtected",
       !config.obs_password.empty()             ? WindowsCrypto::dpapi_protect(config.obs_password)
       : !config.obs_password_protected.empty() ? config.obs_password_protected
                                                : ""}};
  data["vmixPasswordProtected"] =
      !config.vmix_password.empty()             ? WindowsCrypto::dpapi_protect(config.vmix_password)
      : !config.vmix_password_protected.empty() ? config.vmix_password_protected
                                                : "";
  const auto contents = data.dump(2) + '\n';
  const auto temporary = directory_ / "config.json.tmp";
  {
    std::ofstream stream(temporary, std::ios::trunc);
    write_configuration_stream(stream, contents);
    stream.close();
    if (!stream)
      throw std::runtime_error("Could not close configuration temporary file");
  }
  const auto destination = directory_ / "config.json";
  if (!MoveFileExW(temporary.c_str(),
                   destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Could not atomically save configuration");
}
} // namespace studio_remote::windows
