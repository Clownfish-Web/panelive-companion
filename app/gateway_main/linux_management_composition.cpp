#include "linux_management_composition.hpp"
#include <studio_remote/observability/endurance_logging_setting.hpp>

#include "linux_gateway_advertisement.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <studio_remote/ui/management_adapter_rows.hpp>
#include <studio_remote/ui/management_clients.hpp>
#include <utility>

namespace studio_remote::linux
{
namespace
{
std::vector<ManagementClientRow> management_clients(AuditStore& audit, GatewayHttpServer& http)
{
  const auto paired = audit.devices();
  const auto connected = http.connected_devices();
  return make_management_client_rows(
      make_management_clients(paired, connected, http.client_identifications()),
      std::chrono::system_clock::now());
}

ManagementEnduranceStatus management_endurance_status(const EnduranceLogStatus& status)
{
  return {status.enabled,
          status.file.string(),
          status.records_written,
          status.bytes_written,
          status.queued_events,
          status.dropped_events,
          status.last_error,
          status.retained_bytes,
          status.queued_bytes};
}

AudioDiagnosticTap diagnostic_tap(ManagementAudioDiagnosticTap tap)
{
  return tap == ManagementAudioDiagnosticTap::pre_opus ? AudioDiagnosticTap::pre_opus_ipc
                                                       : AudioDiagnosticTap::post_opus;
}

ManagementAudioDiagnosticStatus management_audio_status(const LinuxAudioDiagnosticPlayer& player)
{
  const auto playback = player.status();
  ManagementAudioDiagnosticStatus status;
  status.playing = playback.playing;
  status.tap = playback.tap == AudioDiagnosticTap::pre_opus_ipc
                   ? ManagementAudioDiagnosticTap::pre_opus
                   : ManagementAudioDiagnosticTap::post_opus;
  status.device_name = playback.device_name;
  status.error = playback.error;
  status.received_frames = playback.received_frames;
  status.played_frames = playback.played_frames;
  status.queue_overflows = playback.queue_overflows;
  status.output_underruns = playback.output_underruns;
  status.discontinuities = playback.discontinuities;
  status.silent_concealment_frames = playback.silent_concealment_frames;
  status.queued_frames = playback.queued_frames;
  status.gain_db = playback.gain_db;
  status.maximum_peak_last_second = playback.maximum_peak_last_second;
  status.average_peak_last_second = playback.average_peak_last_second;
  status.duration_limit_reached = playback.duration_limit_reached;
  status.debug_log_active = player.debug_log_active();
  status.debug_log_available = player.debug_log_available();
  return status;
}

void add_client_callbacks(ManagementUiCallbacks& callbacks, const LinuxCompanionServices& services)
{
  callbacks.teleprompter = &services.scripts;
  callbacks.clients = [services] { return management_clients(services.audit, services.http); };
  callbacks.refresh_clients = [] {};
  callbacks.unix_seconds = unix_seconds;
  callbacks.pair_client = [services]
  {
    const auto& config = services.config;
    return ManagementPairing{
        services.pairing.open_default(pairing_url(config), config.default_pairing_role).dump(),
        unix_seconds() + static_cast<std::uint64_t>(default_pairing_lifetime.count() * 60)};
  };
  callbacks.set_operator = [services](std::string_view id)
  {
    services.audit.set_role(id,
                            Role::operator_,
                            services.config.enforce_single_operator_client_mode
                                ? OperatorAssignmentPolicy::transfer_existing_operators
                                : OperatorAssignmentPolicy::allow_multiple_operators);
  };
  callbacks.set_viewer = [services](std::string_view id)
  { services.audit.set_role(id, Role::viewer); };
  callbacks.identify_client = [services](std::string_view id, ClientIdentificationOptions options)
  { services.http.identify_client(id, options); };
  callbacks.identify_all_clients = [services](ClientIdentificationOptions options)
  { services.http.identify_all_clients(options); };
  callbacks.forget_client = [services](std::string_view id)
  {
    services.audit.forget_device(id);
    services.http.disconnect_device(id);
  };
}

void add_obs_connection_callbacks(ManagementUiCallbacks& callbacks,
                                  const LinuxCompanionServices& services)
{
  callbacks.obs_connection = [services]
  {
    const auto& obs = services.obs;
    return ManagementObsConnection{
        services.config.obs_host,
        services.config.obs_port,
        obs.connected() ? "Connected"
                        : (obs.last_error().empty() ? "Connecting" : obs.last_error()),
        obs.connected()};
  };
  callbacks.apply_obs_connection = [services](const ManagementObsConnectionRequest& request)
  {
    auto& config = services.config;
    auto password = config.obs_password;
    if (request.password)
    {
      services.credentials.put("obs-password", *request.password);
      password = *request.password;
    }
    auto updated = config;
    updated.obs_host = request.host;
    updated.obs_port = request.port;
    updated.obs_password = password;
    updated.obs_connection_configured = true;
    services.config_store.save(updated);
    config = std::move(updated);
    services.obs.reconfigure(config.obs_host, config.obs_port, password);
  };
}

void add_adapter_callbacks(ManagementUiCallbacks& callbacks, const LinuxCompanionServices& services)
{
  callbacks.adapters = [services]
  {
    auto& runtime = services.runtime;
    auto detail = runtime.running() ? runtime.last_error() : "adapter gateway stopped";
    if (detail.empty())
      detail = services.obs.last_error();
    return make_management_adapter_rows(
        {{"companion-teleprompter",
          "PaneLive Teleprompter",
          services.teleprompter_runtime.running(),
          services.teleprompter_runtime.state().fresh(),
          "Standalone script sharing; no production software required"},
         {"obs", "OBS Studio", runtime.running(), services.obs.connected(), std::move(detail)}});
  };
  callbacks.start_adapter = [services](std::string_view id)
  { services.adapter_gateways.start(id); };
  callbacks.stop_adapter = [services](std::string_view id) { services.adapter_gateways.stop(id); };
  callbacks.restart_adapter = [services](std::string_view id)
  { services.adapter_gateways.restart(id); };
}

void add_network_callbacks(ManagementUiCallbacks& callbacks, const LinuxCompanionServices& services)
{
  callbacks.network_settings = [services]
  {
    const auto& config = services.config;
    const auto status = services.network.status();
    return ManagementNetworkSettings{config.pairing_address,
                                     available_lan_ipv4_addresses(),
                                     config.port,
                                     status.listening,
                                     status.warning};
  };
  callbacks.apply_network_settings = [services](const ManagementNetworkSettingsRequest& request)
  {
    auto& config = services.config;
    const auto addresses = available_lan_ipv4_addresses();
    if (std::ranges::find(addresses, request.address) == addresses.end())
      throw std::invalid_argument("The selected pairing address is no longer active on this PC");
    if (request.port == config.port && request.address == config.pairing_address &&
        services.network.running_on(request.port))
      return;
    auto updated = config;
    updated.port = request.port;
    updated.pairing_address = request.address;
    services.config_store.save(updated);
    const bool port_changed = config.port != request.port;
    config = std::move(updated);
    if (port_changed || !services.network.running_on(config.port))
      services.network.start(config.port);
  };
}

void add_status_callbacks(ManagementUiCallbacks& callbacks, const LinuxCompanionServices& services)
{
  callbacks.status_feeds = [services]
  {
    const auto capture_health = services.capture.health();
    const auto audio_status = services.audio_monitor.status();
    const auto network = services.http.diagnostics();
    const bool obs_connected = services.obs.connected();
    const bool state_fresh = services.runtime.state().fresh();
    const bool ipc_connected = capture_health.value("ipcConnected", false);
    return std::vector<ManagementStatusRow>{
        {"Companion listener",
         services.http.running() ? "healthy" : "unavailable",
         std::to_string(network.value("openWebSockets", 0U)) + " open WebSocket clients"},
        {"OBS WebSocket",
         obs_connected ? "healthy" : "degraded",
         obs_connected ? "Connected" : services.obs.last_error()},
        {"Canonical OBS state",
         state_fresh ? "healthy" : "degraded",
         state_fresh ? "Current" : "Waiting for reconciliation"},
        {"OBS helper IPC",
         ipc_connected ? "healthy" : "degraded",
         ipc_connected ? "Connected" : "Waiting for plugin"},
        {"OBS monitor audio",
         audio_status.available ? "healthy" : "degraded",
         audio_status.available
             ? "Receiving 48 kHz stereo monitor audio"
             : (audio_status.configured ? audio_status.reason
                                        : "Enable Monitor and Output on an OBS source")}};
  };
}

void add_settings_callbacks(ManagementUiCallbacks& callbacks,
                            const LinuxCompanionServices& services)
{
  callbacks.basic_settings = [services]
  {
    return ManagementBasicSettings{services.config.enforce_single_operator_client_mode,
                                   services.sign_in_startup.enabled()};
  };
  callbacks.set_single_operator_mode = [services](bool enabled)
  {
    if (enabled)
      services.audit.enforce_single_operator();
    auto updated = services.config;
    updated.enforce_single_operator_client_mode = enabled;
    services.config_store.save(updated);
    services.config = std::move(updated);
  };
  callbacks.set_sign_in_startup = [services](bool enabled)
  {
    services.sign_in_startup.set_enabled(enabled);
    auto updated = services.config;
    updated.start_at_login = enabled;
    services.config_store.save(updated);
    services.config = std::move(updated);
  };
  callbacks.endurance_status = [services]
  { return management_endurance_status(services.endurance_log.status()); };
  callbacks.set_endurance_logging = [services](bool enabled)
  { apply_endurance_logging_setting(&services.endurance_log, enabled); };
  callbacks.clear_debug_logs = [services] { services.endurance_log.clear(); };
  callbacks.advanced_settings = [services]
  {
    const auto& config = services.config;
    return ManagementAdvancedSettings{config.automatic_audio_headroom,
                                      config.audio_headroom_per_additional_source_db,
                                      config.audio_source_audition_bus,
                                      {},
                                      {},
                                      {},
                                      "/usr"};
  };
  callbacks.apply_audio_mix = [services](const ManagementAudioMixSettingsRequest& request)
  {
    auto& config = services.config;
    auto updated = config;
    updated.automatic_audio_headroom = request.automatic_headroom;
    updated.audio_headroom_per_additional_source_db = request.headroom_per_additional_source_db;
    updated.audio_source_audition_bus = request.audio_source_audition_bus;
    services.config_store.save(updated);
    config = std::move(updated);
    apply_audio_mix_configuration(config, services.audio_monitor, services.runtime);
  };
  callbacks.shutdown = [services] { services.shutdown.request_stop(); };
}

void add_audio_diagnostic_callbacks(ManagementUiCallbacks& callbacks,
                                    const LinuxCompanionServices& services)
{
  callbacks.audio_outputs = [services]
  {
    std::vector<ManagementAudioOutput> outputs;
    for (auto& device : services.audio_diagnostics.output_devices())
      outputs.push_back({std::move(device.id), std::move(device.name), device.is_default});
    return outputs;
  };
  callbacks.audio_status = [services]
  { return management_audio_status(services.audio_diagnostics); };
  callbacks.start_audio = [services](std::string_view device_id, ManagementAudioDiagnosticTap tap)
  {
    const auto selected = diagnostic_tap(tap);
    services.webrtc.set_post_opus_diagnostic_enabled(false);
    services.audio_diagnostics.start(device_id, selected);
    services.webrtc.set_post_opus_diagnostic_enabled(selected == AudioDiagnosticTap::post_opus);
  };
  callbacks.stop_audio = [services]
  {
    services.webrtc.set_post_opus_diagnostic_enabled(false);
    services.audio_diagnostics.stop();
  };
  callbacks.set_audio_gain = [services](float gain_db)
  { services.audio_diagnostics.set_gain_db(gain_db); };
  callbacks.begin_audio_debug_log = [services]
  {
    const auto state = services.runtime.state().snapshot();
    const auto obs_version = state.value("obs", json::object()).value("version", std::string{});
    services.audio_diagnostics.begin_debug_log(PROJECT_VERSION, obs_version);
  };
  callbacks.complete_audio_debug_log = [services]
  { services.audio_diagnostics.complete_debug_log(); };
  callbacks.audio_debug_report = [services]
  { return services.audio_diagnostics.debug_log_report(); };
}
} // namespace

std::uint64_t unix_seconds()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

void apply_audio_mix_configuration(const GatewayConfig& config,
                                   LinuxAudioMonitor& audio_monitor,
                                   GatewayRuntime& runtime)
{
  audio_monitor.set_mix_settings({config.automatic_audio_headroom,
                                  config.audio_headroom_per_additional_source_db,
                                  config.audio_source_audition_bus});
  runtime.state().set_audio_source_audition_bus_enabled(config.audio_source_audition_bus);
}

ManagementUiCallbacks make_management_callbacks(const LinuxCompanionServices& services)
{
  ManagementUiCallbacks callbacks;
  add_client_callbacks(callbacks, services);
  add_obs_connection_callbacks(callbacks, services);
  add_adapter_callbacks(callbacks, services);
  add_network_callbacks(callbacks, services);
  add_status_callbacks(callbacks, services);
  add_settings_callbacks(callbacks, services);
  add_audio_diagnostic_callbacks(callbacks, services);
  return callbacks;
}
} // namespace studio_remote::linux
