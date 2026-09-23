#pragma once

#include "macos_config_file.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <studio_remote/common/types.hpp>
#include <studio_remote/media/audio_monitor_mix_settings.hpp>
#include <studio_remote/provider/provider_selection.hpp>
#include <studio_remote/ui/management_interface.hpp>

namespace studio_remote
{
class ICredentialStore;
}

namespace studio_remote::macos
{
std::string default_providers();
ProviderSelection selected_providers(std::string_view value);

/// Non-secret settings only. OBS credentials and TLS identity are owned by credential adapters.
struct GatewayConfig
{
  std::string providers = default_providers();
  std::string name;
  std::string instance_id;
  std::uint64_t instance_revision = 1;
  std::uint16_t port = 7443;
  std::string bind_address = "0.0.0.0";
  std::string pairing_address;
  std::string obs_host = "127.0.0.1";
  std::uint16_t obs_port = 4455;
  std::uint16_t streamlabs_port = 59650;
  std::string streamlabs_credential_reference = "streamlabs-api-token";
  bool obs_connection_configured = false;
  std::string obs_credential_reference = "obs-password";
  Role default_pairing_role = Role::viewer;
  bool allow_non_private_interfaces = false;
  bool enforce_single_operator_client_mode = true;
  ManagementInterfaceMode management_interface_mode = ManagementInterfaceMode::basic;
  AudioMonitorMixSettings audio_monitor_mix;
};

struct SavedObsConnection
{
  GatewayConfig config;
  std::string password;
  // A nonempty warning means committed settings must still be published before reporting it.
  std::string warning;
};

/// Blocking filesystem adapter, confined to the application's configuration worker.
/// A first load returns a new UUID and an empty name, matching Windows first-run setup.
class ConfigStore
{
public:
  /// Must throw ConfigurationCommitError only after replacement; all other errors mean no commit.
  /// Captures must outlive this configuration-worker-owned adapter.
  using FileWriter = std::function<void(const std::filesystem::path&, std::string_view)>;
  explicit ConfigStore(std::filesystem::path directory = {},
                       FileWriter writer = write_configuration_file);
  GatewayConfig load() const;
  void save(const GatewayConfig& config) const;
  /// Installer initialization under exclusive instance ownership. Existing files are retained.
  void initialize_installation() const;
  /// Stages Keychain replacement, commits its reference with settings, then cleans old storage.
  /// Callers serialize saves and publish the returned configuration before reconnecting.
  SavedObsConnection save_obs_connection(GatewayConfig config,
                                         ICredentialStore& credentials,
                                         const std::optional<std::string>& replacement) const;
  SavedObsConnection
  save_streamlabs_connection(GatewayConfig config,
                             ICredentialStore& credentials,
                             const std::optional<std::string>& replacement) const;
  const std::filesystem::path& directory() const noexcept;

private:
  SavedObsConnection save_backend_connection(GatewayConfig config,
                                             ICredentialStore& credentials,
                                             const std::optional<std::string>& replacement,
                                             std::string GatewayConfig::* reference,
                                             std::string_view key,
                                             std::string_view label) const;
  const std::filesystem::path directory_;
  const FileWriter writer_;
};
} // namespace studio_remote::macos
