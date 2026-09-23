#pragma once

#include "../credentials/linux_crypto.hpp"

#include <filesystem>
#include <studio_remote/common/types.hpp>
#include <studio_remote/ui/management_interface.hpp>

namespace studio_remote::linux
{
struct GatewayConfig
{
  std::string name = "PaneLive Companion";
  std::string instance_id;
  std::uint64_t instance_revision = 1;
  std::uint16_t port = 7443;
  std::string pairing_address;
  std::string obs_host = "127.0.0.1";
  std::uint16_t obs_port = 4455;
  std::string obs_password;
  bool obs_connection_configured = false;
  Role default_pairing_role = Role::viewer;
  bool allow_non_private_interfaces = false;
  bool enforce_single_operator_client_mode = true;
  bool start_at_login = false;
  ManagementInterfaceMode management_interface_mode = ManagementInterfaceMode::basic;
  bool automatic_audio_headroom = true;
  std::uint32_t audio_headroom_per_additional_source_db = 2;
  bool audio_source_audition_bus = false;
  std::filesystem::path data_directory;
  crypto::CertificateInfo certificate;
};

class ConfigStore
{
public:
  explicit ConfigStore(std::filesystem::path directory = {});
  GatewayConfig load();
  void save(const GatewayConfig& config) const;
  const std::filesystem::path& directory() const noexcept;

private:
  std::filesystem::path directory_;
};

std::filesystem::path default_data_directory();
} // namespace studio_remote::linux
