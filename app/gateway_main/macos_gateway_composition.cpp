#include "macos_gateway_composition.hpp"

#include "../../platform/macos/platform_services/macos_host_diagnostics.hpp"
#include "../../platform/macos/platform_services/macos_lan_address.hpp"
#include "../../platform/macos/platform_services/macos_listener_port.hpp"

#include "../../platform/macos/platform_services/macos_streamlabs_installation.hpp"
#include "../../platform/macos/platform_services/macos_streamlabs_websocket_channel.hpp"
#include <studio_remote/streamlabs/streamlabs_rpc_transport.hpp>
#include <syslog.h>
#include <utility>

#include <studio_remote/discovery/companion_service_record.hpp>
#include <studio_remote/observability/endurance_event_log.hpp>
#include <studio_remote/observability/obs_endurance_snapshot.hpp>

namespace studio_remote::macos
{
MacosGatewayComposition::MacosGatewayComposition(const GatewayConfig& config,
                                                 const std::filesystem::path& data_directory,
                                                 TlsIdentity tls,
                                                 std::string obs_password,
                                                 ICredentialStore& credentials,
                                                 AuditStore& audit,
                                                 IHotkeyExecutor& system_hotkeys,
                                                 EnduranceEventLog& log,
                                                 GatewayCompositionInputs inputs)
    : tls_(std::move(tls)), identity_{config.instance_id, config.name, tls_.fingerprint()},
      audit_(audit), log_(log), providers_(selected_providers(config.providers)),
      pairing_(identity_, audit, credentials, providers_.ids),
      obs_(config.obs_host, config.obs_port, std::move(obs_password), std::move(inputs.client)),
      streamlabs_(inputs.streamlabs_transport
                      ? std::move(inputs.streamlabs_transport)
                      : std::make_unique<streamlabs::StreamlabsRpcTransport>(
                            std::make_unique<MacosStreamlabsWebSocketChannel>(
                                config.streamlabs_port,
                                providers_.contains("streamlabs")
                                    ? credentials.get(config.streamlabs_credential_reference)
                                    : std::string{},
                                verified_streamlabs_version))),
      hotkeys_(system_hotkeys, hotkey_targets(), audit), thumbnails_(std::move(inputs.thumbnails)),
      audio_roster_(std::move(inputs.audio_roster)),
      scene_properties_(std::move(inputs.scene_properties)), runtime_(identity_,
                                                                      obs_,
                                                                      audit,
                                                                      &thumbnails_,
                                                                      &thumbnail_encoder_,
                                                                      &audio_roster_,
                                                                      &scene_properties_,
                                                                      &source_media_inspector_),
      webrtc_(runtime_.state(), &realtime_audio_),
      streamlabs_runtime_(identity_, streamlabs_, audit),
      streamlabs_webrtc_(streamlabs_runtime_.state(), &realtime_audio_),
      teleprompter_runtime_(identity_, teleprompter_, audit),
      teleprompter_webrtc_(teleprompter_runtime_.state()), diagnostics_(&realtime_audio_),
      media_delivery_(
          runtime_.state(),
          webrtc_,
          [this](const AudioMonitorStatus& status) { diagnostics_.set_monitor_status(status); },
          &log),
      audio_(
          [this](SharedAudioMonitorFrame frame)
          {
            diagnostics_.push(frame, AudioDiagnosticTap::pre_opus_ipc);
            webrtc_.push_audio_monitor(std::move(frame));
          },
          [this](const AudioMonitorStatus& status)
          { media_delivery_.receive_audio_status(status); },
          &realtime_audio_,
          std::move(inputs.media.audio),
          std::move(inputs.media.audio_control)),
      capture_([this](std::string_view track, std::shared_ptr<const RawVideoFrame> frame)
               { media_delivery_.receive_video(track, std::move(frame)); },
               [this](std::string_view track, const json& health)
               { media_delivery_.receive_video_health(track, health); },
               [this] { return webrtc_.video_demand(); },
               [this](std::uint32_t generation)
               { media_delivery_.receive_capture_generation(generation); },
               std::move(inputs.media.video),
               std::move(inputs.media.video_control)),
      network_config_(http_config(config, data_directory)),
      http_(network_config_, provider_sessions(), pairing_, hotkeys_, &log, inputs.scripts),
      allow_non_private_interfaces_(config.allow_non_private_interfaces),
      network_(listener_binding(), network_config_.listening_ports), adapters_(adapter_bindings())
{
  obs_.on_diagnostic_event(
      [&log](std::string_view event, json details)
      {
        if (log.enabled())
          log.record("obsWebSocket", event, std::move(details));
      });
  runtime_.state().subscribe(
      [&log](std::uint64_t revision, const json& changed)
      {
        if (log.enabled())
          log.record(
              "state", "canonicalStateChanged", {{"revision", revision}, {"changed", changed}});
      });
  webrtc_.on_diagnostic_event(
      [&log](std::string_view event, json details)
      {
        if (log.enabled())
          log.record("webrtc", event, std::move(details));
      });
  audio_.set_mix_settings(config.audio_monitor_mix);
  runtime_.state().set_audio_source_audition_bus_enabled(
      config.audio_monitor_mix.activate_audio_source_audition_bus);
  webrtc_.set_post_opus_diagnostic_handler(
      [this](SharedAudioMonitorFrame frame)
      { diagnostics_.push(std::move(frame), AudioDiagnosticTap::post_opus); });
  webrtc_.set_audio_monitor_status(audio_.status());
  diagnostics_.set_monitor_status(audio_.status());
  media_delivery_.on_capabilities_changed([this] { http_.publish_capabilities("obs"); });
  media_delivery_.on_capture_generation_changed(
      [this](std::uint32_t generation) { http_.reinitialize_media_sessions("obs", generation); });
  endurance_snapshots_.emplace(log_,
                               make_obs_endurance_snapshot_provider(
                                   {[this] { return audio_.status(); },
                                    [this] { return audit_.devices(); },
                                    macos_host_diagnostics,
                                    [this] { return runtime_.state().snapshot(); },
                                    [this] { return obs_.connected(); },
                                    [this] { return runtime_.state().fresh(); },
                                    [this] { return runtime_.last_error(); },
                                    [this] { return obs_.last_error(); },
                                    [this] { return capture_.health(); },
                                    [this] { return runtime_.audio_mixer_diagnostics(); },
                                    [this] { return runtime_.thumbnail_diagnostics(); },
                                    [this] { return runtime_.scene_item_properties_diagnostics(); },
                                    [this] { return runtime_.critical_queue_health(); },
                                    [this] { return webrtc_.health(); },
                                    [this] { return http_.diagnostics(); }}));
  audit_.on_roles_changed([this](const std::vector<DeviceRoleChange>& changes)
                          { http_.apply_role_changes(changes); });
}

MacosGatewayComposition::~MacosGatewayComposition()
{
  audit_.on_roles_changed({});
  pairing_.on_closed({});
  stop();
  endurance_snapshots_.reset();
  webrtc_.set_post_opus_diagnostic_handler({});
  webrtc_.on_diagnostic_event({});
  media_delivery_.on_capabilities_changed({});
  media_delivery_.on_capture_generation_changed({});
  // OBS outlives the runtime callbacks installed by GatewayRuntime::start. Join before clearing
  // them so no callback target can disappear while the adapter's reader is still running.
  obs_.on_diagnostic_event({});
  obs_.on_event({});
  obs_.on_connection({});
}

HttpConfig MacosGatewayComposition::http_config(const GatewayConfig& config,
                                                const std::filesystem::path& data_directory)
{
  HttpConfig result;
  result.port = config.port;
  result.listening_ports = listener_specification(config.port, config.allow_non_private_interfaces);
  result.error_log = std::filesystem::absolute(data_directory / "civetweb-error.log");
  result.identity = identity_;
  result.initialize_tls_context = [this](ssl_ctx_st& context)
  { tls_.configure_server_context(context); };
  return result;
}

void MacosGatewayComposition::start()
{
  if (http_.running())
    return;
  start_adapters();
  network_.start(network_config_.port);
}

ListenerGatewayBinding MacosGatewayComposition::listener_binding()
{
  return {check_listener_port_availability,
          [this](std::uint16_t port)
          { return listener_specification(port, allow_non_private_interfaces_); },
          [this] { return http_.running(); },
          [this] { discovery_.stop(); },
          [this] { http_.stop(); },
          [this](std::uint16_t port, const std::string& listeners)
          {
            auto requested = network_config_;
            requested.port = port;
            requested.listening_ports = listeners;
            http_.configure(requested);
            network_config_ = std::move(requested);
          },
          [this] { http_.start(); },
          [this](std::uint16_t port)
          {
            discovery_.advertise(
                make_companion_service_record(identity_, port, providers_.normalized));
          },
          [this](std::string_view)
          {
            log_.record("discovery", "advertisementFailed");
            syslog(LOG_WARNING, "Companion Bonjour advertisement failed");
          }};
}

void MacosGatewayComposition::reconfigure_network(std::uint16_t port,
                                                  bool allow_non_private_interfaces)
{
  allow_non_private_interfaces_ = allow_non_private_interfaces;
  network_.start(port);
}

ListenerGatewayStatus MacosGatewayComposition::network_status() const
{
  return network_.status();
}

bool MacosGatewayComposition::listener_running_on(std::uint16_t port) const
{
  return network_.running_on(port);
}

void MacosGatewayComposition::stop()
{
  http_.on_role_change_requested({});
  http_.on_global_setting_change_requested({});
  for (const auto& id : providers_.ids)
    adapters_.stop(id);
  http_.stop();
  webrtc_.set_post_opus_diagnostic_enabled(false);
  diagnostics_.stop();
  discovery_.stop();
}

GatewayHttpServer& MacosGatewayComposition::http() noexcept
{
  return http_;
}
PairingManager& MacosGatewayComposition::pairing() noexcept
{
  return pairing_;
}
ObsClient& MacosGatewayComposition::obs() noexcept
{
  return obs_;
}
GatewayRuntime& MacosGatewayComposition::runtime() noexcept
{
  return runtime_;
}
WebRtcHub& MacosGatewayComposition::webrtc() noexcept
{
  return webrtc_;
}
MacosAudioDiagnosticPlayer& MacosGatewayComposition::audio_diagnostics() noexcept
{
  return diagnostics_;
}
MacosAudioMonitor& MacosGatewayComposition::audio_monitor() noexcept
{
  return audio_;
}
MacosCaptureWorker& MacosGatewayComposition::capture() noexcept
{
  return capture_;
}
AdapterGatewayController& MacosGatewayComposition::adapters() noexcept
{
  return adapters_;
}
DiscoveryStatus MacosGatewayComposition::discovery_status() const noexcept
{
  return discovery_.status();
}
const GatewayIdentity& MacosGatewayComposition::identity() const noexcept
{
  return identity_;
}
void MacosGatewayComposition::reconfigure_streamlabs(std::uint16_t port, std::string token)
{
  streamlabs_.reconfigure(std::make_unique<streamlabs::StreamlabsRpcTransport>(
      std::make_unique<MacosStreamlabsWebSocketChannel>(
          port, std::move(token), verified_streamlabs_version)));
}

GatewayRuntime& MacosGatewayComposition::streamlabs_runtime() noexcept
{
  return streamlabs_runtime_;
}
streamlabs::StreamlabsAdapter& MacosGatewayComposition::streamlabs_backend() noexcept
{
  return streamlabs_;
}
const ProviderSelection& MacosGatewayComposition::providers() const noexcept
{
  return providers_;
}
GatewayRuntime& MacosGatewayComposition::teleprompter_runtime() noexcept
{
  return teleprompter_runtime_;
}
void MacosGatewayComposition::start_adapters()
{
  adapters_.start("companion-teleprompter");
  for (const auto& id : providers_.ids)
    if (id != "companion-teleprompter")
      adapters_.start(id);
}
std::vector<ProviderSession> MacosGatewayComposition::provider_sessions()
{
  std::vector<ProviderSession> result;
  result.push_back({"companion-teleprompter", &teleprompter_runtime_, &teleprompter_webrtc_});
  if (providers_.contains("obs"))
    result.push_back({"obs", &runtime_, &webrtc_});
  if (providers_.contains("streamlabs"))
    result.push_back({"streamlabs", &streamlabs_runtime_, &streamlabs_webrtc_});
  return result;
}
std::vector<HotkeyService::ProductionTarget> MacosGatewayComposition::hotkey_targets()
{
  std::vector<HotkeyService::ProductionTarget> result;
  result.push_back({"companion-teleprompter", &teleprompter_});
  if (providers_.contains("obs"))
    result.push_back({"obs", &obs_});
  if (providers_.contains("streamlabs"))
    result.push_back({"streamlabs", &streamlabs_});
  return result;
}
std::vector<AdapterGatewayBinding> MacosGatewayComposition::adapter_bindings()
{
  std::vector<AdapterGatewayBinding> result;
  result.push_back({"companion-teleprompter",
                    [this] { return teleprompter_runtime_.running(); },
                    [this] { teleprompter_runtime_.start(); },
                    [this] { teleprompter_runtime_.stop(); },
                    [] {},
                    [] {}});
  if (providers_.contains("obs"))
    result.push_back({"obs",
                      [this] { return runtime_.running(); },
                      [this] { runtime_.start(); },
                      [this] { runtime_.stop(); },
                      [this]
                      {
                        webrtc_.start();
                        audio_.start();
                        capture_.start();
                      },
                      [this]
                      {
                        capture_.stop();
                        audio_.stop();
                        webrtc_.stop();
                      }});
  if (providers_.contains("streamlabs"))
    result.push_back({"streamlabs",
                      [this] { return streamlabs_runtime_.running(); },
                      [this] { streamlabs_runtime_.start(); },
                      [this] { streamlabs_runtime_.stop(); },
                      [this] { streamlabs_webrtc_.start(); },
                      [this] { streamlabs_webrtc_.stop(); }});
  return result;
}
} // namespace studio_remote::macos
