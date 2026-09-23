#pragma once

#include "../../core/application/gateway.hpp"
#include "../../core/transport/libdatachannel_transport.hpp"
#include "../../platform/macos/capture/macos_audio_diagnostic_player.hpp"
#include "../../platform/macos/capture/macos_audio_mixer_roster.hpp"
#include "../../platform/macos/capture/macos_audio_monitor.hpp"
#include "../../platform/macos/capture/macos_capture_worker.hpp"
#include "../../platform/macos/capture/macos_scene_item_properties.hpp"
#include "../../platform/macos/capture/macos_source_media_inspector.hpp"
#include "../../platform/macos/capture/macos_thumbnail_capture.hpp"
#include "../../platform/macos/capture/macos_thumbnail_encoder.hpp"
#include "../../platform/macos/credentials/macos_tls_identity.hpp"
#include "../../platform/macos/discovery/macos_dns_sd.hpp"
#include "../../platform/macos/ipc/macos_media_ipc_names.hpp"
#include "../../platform/macos/obs/macos_obs_client.hpp"
#include "../../platform/macos/platform_services/macos_config.hpp"
#include "../../platform/macos/platform_services/macos_realtime_audio_services.hpp"
#include "adapter_gateway_controller.hpp"
#include "http_server.hpp"
#include "listener_gateway_controller.hpp"
#include "obs_media_delivery.hpp"
#include <studio_remote/teleprompter/adapter.hpp>

#include <studio_remote/observability/endurance_snapshot_registration.hpp>

#include <optional>
#include <studio_remote/streamlabs/streamlabs_adapter.hpp>

namespace studio_remote::macos
{
struct GatewayCompositionInputs
{
  // Application-owned repository outlives the gateway and its HTTP borrower.
  teleprompter::IRepository* scripts = nullptr;
  ObsClientInputs client;
  MediaIpcNames media;
  std::string audio_roster = ipc_names::audio_mixer_roster;
  std::string scene_properties = ipc_names::scene_item_properties;
  std::string thumbnails = ipc_names::source_thumbnails;
  std::unique_ptr<streamlabs::IStreamlabsTransport> streamlabs_transport = {};
};

/// Owned HTTPS/WSS, pairing, and OBS provider assembly. The configuration worker is the single
/// lifecycle owner. Credentials, audit, system hotkeys, and log outlive this object; this object
/// owns the audit role-change callback. UI users must detach before destruction.
/// The application applies persisted startup operator policy before constructing this assembly.
class MacosGatewayComposition final
{
public:
  MacosGatewayComposition(const GatewayConfig& config,
                          const std::filesystem::path& data_directory,
                          TlsIdentity tls,
                          std::string obs_password,
                          ICredentialStore& credentials,
                          AuditStore& audit,
                          IHotkeyExecutor& system_hotkeys,
                          EnduranceEventLog& log,
                          GatewayCompositionInputs inputs = {});
  ~MacosGatewayComposition();
  MacosGatewayComposition(const MacosGatewayComposition&) = delete;
  MacosGatewayComposition& operator=(const MacosGatewayComposition&) = delete;

  void start();
  void stop();
  /// Configuration-worker operation using the shared Windows preflight/recovery sequence.
  void reconfigure_network(std::uint16_t port, bool allow_non_private_interfaces);
  ListenerGatewayStatus network_status() const;
  bool listener_running_on(std::uint16_t port) const;
  GatewayHttpServer& http() noexcept;
  PairingManager& pairing() noexcept;
  ObsClient& obs() noexcept;
  GatewayRuntime& runtime() noexcept;
  WebRtcHub& webrtc() noexcept;
  MacosAudioDiagnosticPlayer& audio_diagnostics() noexcept;
  MacosAudioMonitor& audio_monitor() noexcept;
  MacosCaptureWorker& capture() noexcept;
  AdapterGatewayController& adapters() noexcept;
  GatewayRuntime& streamlabs_runtime() noexcept;
  GatewayRuntime& teleprompter_runtime() noexcept;
  const ProviderSelection& providers() const noexcept;
  void start_adapters();
  streamlabs::StreamlabsAdapter& streamlabs_backend() noexcept;
  void reconfigure_streamlabs(std::uint16_t port, std::string token);
  DiscoveryStatus discovery_status() const noexcept;
  const GatewayIdentity& identity() const noexcept;

private:
  HttpConfig http_config(const GatewayConfig& config, const std::filesystem::path& data_directory);
  ListenerGatewayBinding listener_binding();
  std::vector<ProviderSession> provider_sessions();
  std::vector<HotkeyService::ProductionTarget> hotkey_targets();
  std::vector<AdapterGatewayBinding> adapter_bindings();

  const TlsIdentity tls_;
  const GatewayIdentity identity_;
  AuditStore& audit_;
  EnduranceEventLog& log_;
  const ProviderSelection providers_;
  PairingManager pairing_;
  ObsClient obs_;
  streamlabs::StreamlabsAdapter streamlabs_;
  teleprompter::Adapter teleprompter_;
  HotkeyService hotkeys_;
  MacosThumbnailCapture thumbnails_;
  MacosThumbnailEncoder thumbnail_encoder_;
  MacosAudioMixerRoster audio_roster_;
  MacosSceneItemProperties scene_properties_;
  MacosSourceMediaInspector source_media_inspector_;
  GatewayRuntime runtime_;
  MacosRealtimeAudioServices realtime_audio_;
  WebRtcHub webrtc_;
  GatewayRuntime streamlabs_runtime_;
  WebRtcHub streamlabs_webrtc_;
  GatewayRuntime teleprompter_runtime_;
  WebRtcHub teleprompter_webrtc_;
  MacosAudioDiagnosticPlayer diagnostics_;
  ObsMediaDelivery media_delivery_;
  MacosAudioMonitor audio_;
  MacosCaptureWorker capture_;
  HttpConfig network_config_;
  GatewayHttpServer http_;
  MacosDnsSd discovery_;
  bool allow_non_private_interfaces_;
  ListenerGatewayController network_;
  AdapterGatewayController adapters_;
  std::optional<EnduranceSnapshotRegistration> endurance_snapshots_;
};
} // namespace studio_remote::macos
