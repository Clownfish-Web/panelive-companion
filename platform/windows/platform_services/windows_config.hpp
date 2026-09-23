#pragma once
#include "../credentials/windows_crypto.hpp"
#include <filesystem>
#include <studio_remote/common/types.hpp>
#include <studio_remote/media/audio_monitor_mix_settings.hpp>
#include <studio_remote/ui/management_interface.hpp>

namespace studio_remote::windows
{
struct GatewayConfig
{
  std::string name;
  std::string instance_id;
  uint64_t instance_revision = 1;
  uint16_t port = 7443;
  std::string bind_address = "0.0.0.0";
  std::string pairing_address;
  std::string obs_host = "127.0.0.1";
  uint16_t obs_port = 4455;
  std::string obs_password;
  std::string obs_password_protected;
  bool obs_connection_configured = false;
  std::string vmix_host = "127.0.0.1";
  uint16_t vmix_http_port = 8088;
  std::string vmix_username;
  std::string vmix_password;
  std::string vmix_password_protected;
  bool vmix_connection_configured = false;
  bool vmix_streaming_configured = false;
  std::uint8_t vmix_srt_program_output = 1;
  std::uint8_t vmix_srt_preview_output = 2;
  std::uint16_t vmix_srt_program_port = 10'000;
  std::uint16_t vmix_srt_preview_port = 10'001;
  std::uint32_t vmix_srt_latency_ms = 160;
  std::string vmix_program_ndi_source;
  std::string vmix_preview_ndi_source;
  // Operator override for the vMix installation that supplies the NDI runtime. Empty means the
  // installed location is discovered automatically.
  std::string vmix_installation_directory;
  Role default_pairing_role = Role::viewer;
  bool allow_non_private_interfaces = false;
  bool enforce_single_operator_client_mode = true;
  ManagementInterfaceMode management_interface_mode = ManagementInterfaceMode::basic;
  AudioMonitorMixSettings audio_monitor_mix;
  std::filesystem::path data_directory;
  WindowsCrypto::CertificateInfo certificate;
};

class ConfigStore
{
public:
  explicit ConfigStore(std::filesystem::path directory = {});
  GatewayConfig load();
  void save(const GatewayConfig& config);
  const std::filesystem::path& directory() const
  {
    return directory_;
  }

private:
  std::filesystem::path directory_;
};
} // namespace studio_remote::windows
