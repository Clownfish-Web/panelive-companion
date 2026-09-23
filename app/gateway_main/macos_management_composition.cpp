#include "macos_management_composition.hpp"

#include "../../platform/macos/platform_services/macos_lan_address.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <studio_remote/observability/endurance_logging_setting.hpp>
#include <studio_remote/ui/management_adapter_rows.hpp>
#include <studio_remote/ui/management_clients.hpp>
#include <studio_remote/ui/management_status_rows.hpp>
#include <syslog.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
std::uint64_t unix_seconds()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
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

ManagementAudioDiagnosticStatus management_audio_status(const MacosAudioDiagnosticPlayer& player)
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

void add_client_callbacks(ManagementUiCallbacks& callbacks, const MacosManagementServices& services)
{
  callbacks.teleprompter = services.scripts;
  callbacks.clients = [services]
  {
    const auto paired = services.audit.devices();
    const auto connected = services.gateway.http().connected_devices();
    return make_management_client_rows(
        make_management_clients(
            paired, connected, services.gateway.http().client_identifications()),
        std::chrono::system_clock::now());
  };
  callbacks.unix_seconds = unix_seconds;
  callbacks.pair_client = [services]
  {
    const auto& config = services.config;
    if (config.pairing_address.empty())
      throw std::runtime_error(
          "No active private IPv4 LAN address is available for mobile pairing");
    const auto payload = services.gateway.pairing().open_default(
        "https://" + config.pairing_address + ":" + std::to_string(config.port),
        config.default_pairing_role);
    return ManagementPairing{payload.dump(),
                             payload.at("expiresAtUnixSeconds").get<std::uint64_t>()};
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
  { services.gateway.http().identify_client(id, options); };
  callbacks.identify_all_clients = [services](ClientIdentificationOptions options)
  { services.gateway.http().identify_all_clients(options); };
  callbacks.forget_client = [services](std::string_view id)
  {
    services.audit.forget_device(id);
    services.gateway.http().disconnect_device(id);
  };
}

void add_obs_connection_callbacks(ManagementUiCallbacks& callbacks,
                                  const MacosManagementServices& services)
{
  callbacks.obs_connection = [services]
  {
    const auto& obs = services.gateway.obs();
    auto& runtime = services.gateway.runtime();
    auto detail = runtime.last_error();
    if (detail.empty())
      detail = obs.last_error();
    return make_management_obs_connection({services.config.obs_host,
                                           services.config.obs_port,
                                           obs.connected(),
                                           runtime.state().fresh(),
                                           std::move(detail),
                                           "OBS Studio",
                                           true});
  };
  callbacks.apply_obs_connection = [services](const ManagementObsConnectionRequest& request)
  {
    auto& config = services.config;
    auto updated = config;
    updated.obs_host = request.host;
    updated.obs_port = request.port;
    updated.obs_connection_configured = true;
    auto saved = services.config_store.save_obs_connection(
        std::move(updated), services.credentials, request.password);
    config = std::move(saved.config);
    if (!saved.warning.empty())
      ::syslog(LOG_WARNING, "%s", saved.warning.c_str());
    services.gateway.obs().reconfigure(config.obs_host, config.obs_port, saved.password);
    if (!saved.warning.empty())
      throw std::runtime_error(saved.warning);
  };
}

void add_streamlabs_connection_callbacks(ManagementUiCallbacks& callbacks,
                                         const MacosManagementServices& services)
{
  callbacks.streamlabs_connection = [services]
  {
    auto& adapter = services.gateway.streamlabs_backend();
    auto& runtime = services.gateway.streamlabs_runtime();
    auto detail = runtime.last_error();
    if (detail.empty())
      detail = adapter.last_error();
    return make_management_obs_connection({"127.0.0.1",
                                           services.config.streamlabs_port,
                                           adapter.connected(),
                                           runtime.state().fresh(),
                                           std::move(detail),
                                           "Streamlabs Desktop",
                                           true});
  };
  callbacks.apply_streamlabs_connection = [services](const ManagementObsConnectionRequest& request)
  {
    auto updated = services.config;
    updated.streamlabs_port = request.port;
    auto saved = services.config_store.save_streamlabs_connection(
        std::move(updated), services.credentials, request.password);
    services.config = std::move(saved.config);
    services.gateway.reconfigure_streamlabs(services.config.streamlabs_port,
                                            std::move(saved.password));
    if (!saved.warning.empty())
      throw std::runtime_error(saved.warning);
  };
}

void add_adapter_callbacks(ManagementUiCallbacks& callbacks,
                           const MacosManagementServices& services)
{
  callbacks.adapters = [services]
  {
    auto& runtime = services.gateway.runtime();
    auto& obs = services.gateway.obs();
    auto detail = runtime.running() ? runtime.last_error() : "adapter gateway stopped";
    if (detail.empty())
      detail = obs.last_error();
    auto& streamlabs = services.gateway.streamlabs_backend();
    auto& streamlabs_runtime = services.gateway.streamlabs_runtime();
    auto streamlabs_detail =
        streamlabs_runtime.running() ? streamlabs_runtime.last_error() : "adapter gateway stopped";
    if (streamlabs_detail.empty())
      streamlabs_detail = streamlabs.last_error();
    std::vector<AdapterGatewayStatus> statuses;
    const auto& providers = services.gateway.providers();
    {
      auto& prompting = services.gateway.teleprompter_runtime();
      statuses.push_back({"companion-teleprompter",
                          "PaneLive Teleprompter",
                          prompting.running(),
                          prompting.state().fresh(),
                          "Standalone script sharing; no production software required"});
    }
    if (providers.contains("obs"))
      statuses.push_back(
          {"obs", "OBS Studio", runtime.running(), runtime.running() && obs.connected(), detail});
    if (providers.contains("streamlabs"))
      statuses.push_back({"streamlabs",
                          "Streamlabs Desktop",
                          streamlabs_runtime.running(),
                          streamlabs_runtime.running() && streamlabs.connected(),
                          streamlabs_detail});
    return make_management_adapter_rows(statuses);
  };
  callbacks.start_adapter = [services](std::string_view id)
  { services.gateway.adapters().start(id); };
  callbacks.stop_adapter = [services](std::string_view id)
  { services.gateway.adapters().stop(id); };
  callbacks.restart_adapter = [services](std::string_view id)
  { services.gateway.adapters().restart(id); };
}

void add_network_callbacks(ManagementUiCallbacks& callbacks,
                           const MacosManagementServices& services)
{
  callbacks.network_settings = [services]
  {
    const auto& config = services.config;
    const auto network = services.gateway.network_status();
    return ManagementNetworkSettings{config.pairing_address,
                                     available_lan_ipv4_addresses(),
                                     config.port,
                                     network.listening,
                                     network.warning};
  };
  callbacks.apply_network_settings = [services](const ManagementNetworkSettingsRequest& request)
  {
    constexpr auto warning = "Network settings saved, but configuration durability is unconfirmed";
    auto& config = services.config;
    const auto addresses = available_lan_ipv4_addresses();
    if (std::ranges::find(addresses, request.address) == addresses.end())
      throw std::invalid_argument("The selected pairing address is no longer active on this PC");
    if (request.port == config.port && request.address == config.pairing_address &&
        services.gateway.listener_running_on(request.port))
      return;
    auto updated = config;
    updated.port = request.port;
    updated.pairing_address = request.address;
    bool durability_unconfirmed = false;
    try
    {
      services.config_store.save(updated);
    }
    catch (const ConfigurationCommitError&)
    {
      // Rename selected the requested endpoint. Publish it before Windows listener recovery.
      durability_unconfirmed = true;
      ::syslog(LOG_WARNING, "%s", warning);
    }
    const bool port_changed = config.port != request.port;
    config = std::move(updated);
    if (port_changed || !services.gateway.listener_running_on(config.port))
      services.gateway.reconfigure_network(config.port, config.allow_non_private_interfaces);
    if (durability_unconfirmed)
      throw std::runtime_error(warning);
  };
}

void add_status_callbacks(ManagementUiCallbacks& callbacks, const MacosManagementServices& services)
{
  callbacks.status_feeds = [services]
  {
    auto& runtime = services.gateway.runtime();
    auto& obs = services.gateway.obs();
    auto detail = runtime.last_error();
    if (detail.empty())
      detail = obs.last_error();
    const ObsConnectionStatus obs_status{services.config.obs_host,
                                         services.config.obs_port,
                                         obs.connected(),
                                         runtime.state().fresh(),
                                         std::move(detail),
                                         "OBS Studio",
                                         true};
    const auto state = runtime.state().snapshot();
    const auto obs_version = state.value("obs", json::object()).value("version", std::string{});
    return make_management_status_rows(
        services.status_snapshot_builder.build({services.config.name,
                                                services.config.port,
                                                services.gateway.network_status().listener_ports},
                                               obs_status,
                                               obs_version,
                                               services.gateway.capture().health(),
                                               runtime.audio_mixer_diagnostics(),
                                               runtime.thumbnail_diagnostics(),
                                               services.gateway.webrtc().health()));
  };
}

void add_access_settings_callbacks(ManagementUiCallbacks& callbacks,
                                   const MacosManagementServices& services)
{
  callbacks.basic_settings = [services]
  {
    return ManagementBasicSettings{services.config.enforce_single_operator_client_mode,
                                   services.sign_in_startup_enabled()};
  };
  callbacks.set_single_operator_mode = [services](bool enabled)
  {
    constexpr auto warning =
        "Single-operator setting saved, but configuration durability is unconfirmed";
    if (enabled)
      services.audit.enforce_single_operator();
    auto updated = services.config;
    updated.enforce_single_operator_client_mode = enabled;
    bool durability_unconfirmed = false;
    try
    {
      services.config_store.save(updated);
    }
    catch (const ConfigurationCommitError&)
    {
      // The visible file already selects this policy. Complete Windows publication order.
      durability_unconfirmed = true;
      ::syslog(LOG_WARNING, "%s", warning);
    }
    services.config = std::move(updated);
    services.endurance_log.record(
        "configuration", "singleOperatorClientModeChanged", {{"enabled", enabled}});
    if (durability_unconfirmed)
      throw std::runtime_error(warning);
  };
  callbacks.set_sign_in_startup = [services](bool enabled)
  {
    services.set_sign_in_startup(enabled);
    services.endurance_log.record("configuration", "signInStartupChanged", {{"enabled", enabled}});
  };
}

void add_endurance_callbacks(ManagementUiCallbacks& callbacks,
                             const MacosManagementServices& services)
{
  callbacks.endurance_status = [services]
  { return management_endurance_status(services.endurance_log.status()); };
  callbacks.set_endurance_logging = [services](bool enabled)
  { apply_endurance_logging_setting(&services.endurance_log, enabled); };
  callbacks.clear_debug_logs = [services] { services.endurance_log.clear(); };
}

void add_audio_mix_callbacks(ManagementUiCallbacks& callbacks,
                             const MacosManagementServices& services)
{
  callbacks.advanced_settings = [services]
  {
    const auto& config = services.config;
    return ManagementAdvancedSettings{config.audio_monitor_mix.automatic_headroom,
                                      config.audio_monitor_mix.attenuation_per_additional_source_db,
                                      config.audio_monitor_mix.activate_audio_source_audition_bus,
                                      {},
                                      {},
                                      {},
                                      services.obs_directory ? services.obs_directory() : ""};
  };
  callbacks.apply_audio_mix = [services](const ManagementAudioMixSettingsRequest& request)
  {
    constexpr auto warning = "Audio settings saved, but configuration durability is unconfirmed";
    auto& config = services.config;
    auto updated = config;
    updated.audio_monitor_mix.automatic_headroom = request.automatic_headroom;
    updated.audio_monitor_mix.attenuation_per_additional_source_db =
        request.headroom_per_additional_source_db;
    updated.audio_monitor_mix.activate_audio_source_audition_bus =
        request.audio_source_audition_bus;
    bool durability_unconfirmed = false;
    try
    {
      services.config_store.save(updated);
    }
    catch (const ConfigurationCommitError&)
    {
      // Apply the settings selected by rename even when directory durability is uncertain.
      durability_unconfirmed = true;
      ::syslog(LOG_WARNING, "%s", warning);
    }
    config = std::move(updated);
    services.gateway.audio_monitor().set_mix_settings(config.audio_monitor_mix);
    services.gateway.runtime().state().set_audio_source_audition_bus_enabled(
        config.audio_monitor_mix.activate_audio_source_audition_bus);
    if (durability_unconfirmed)
      throw std::runtime_error(warning);
  };
}

void add_audio_diagnostic_callbacks(ManagementUiCallbacks& callbacks,
                                    const MacosManagementServices& services)
{
  callbacks.audio_outputs = [services]
  {
    std::vector<ManagementAudioOutput> outputs;
    for (auto& device : services.gateway.audio_diagnostics().output_devices())
      outputs.push_back({std::move(device.id), std::move(device.name), device.is_default});
    return outputs;
  };
  callbacks.audio_status = [services]
  { return management_audio_status(services.gateway.audio_diagnostics()); };
  callbacks.start_audio = [services](std::string_view device_id, ManagementAudioDiagnosticTap tap)
  {
    const auto selected = diagnostic_tap(tap);
    services.gateway.webrtc().set_post_opus_diagnostic_enabled(false);
    services.gateway.audio_diagnostics().start(device_id, selected);
    services.gateway.webrtc().set_post_opus_diagnostic_enabled(selected ==
                                                               AudioDiagnosticTap::post_opus);
  };
  callbacks.stop_audio = [services]
  {
    services.gateway.webrtc().set_post_opus_diagnostic_enabled(false);
    services.gateway.audio_diagnostics().stop();
  };
  callbacks.set_audio_gain = [services](float gain_db)
  { services.gateway.audio_diagnostics().set_gain_db(gain_db); };
  callbacks.begin_audio_debug_log = [services]
  {
    const auto state = services.gateway.runtime().state().snapshot();
    const auto obs_version = state.value("obs", json::object()).value("version", std::string{});
    services.gateway.audio_diagnostics().begin_debug_log(services.build_version, obs_version);
  };
  callbacks.complete_audio_debug_log = [services]
  { services.gateway.audio_diagnostics().complete_debug_log(); };
  callbacks.audio_debug_report = [services]
  { return services.gateway.audio_diagnostics().debug_log_report(); };
}
} // namespace

ClientApprovalCallbacks make_macos_client_approval_callbacks(
    GatewayConfig& config,
    ConfigStore& store,
    AuditStore& audit,
    MacosGatewayComposition& gateway,
    std::function<bool(RoleChangeRequest)> present_role,
    std::function<bool(GlobalSettingChangeRequest)> present_global_setting,
    std::function<void(std::string_view)> cancel_device)
{
  return make_client_approval_callbacks(
      audit,
      {[&config] { return config.enforce_single_operator_client_mode; },
       [&config, &store, &gateway](bool enabled)
       {
         auto updated = config;
         updated.audio_monitor_mix.activate_audio_source_audition_bus = enabled;
         try
         {
           store.save(updated);
         }
         catch (const ConfigurationCommitError&)
         {
           // The approved value is already selected on disk. Finish application before the
           // shared approval callback audits and completes it; a warning must not reject it.
           syslog(LOG_WARNING,
                  "Approved audio settings saved, but configuration durability is unconfirmed");
         }
         config = std::move(updated);
         gateway.audio_monitor().set_mix_settings(config.audio_monitor_mix);
         gateway.runtime().state().set_audio_source_audition_bus_enabled(
             config.audio_monitor_mix.activate_audio_source_audition_bus);
       },
       [&gateway](std::string_view device_id, std::string_view request_id, Role role, bool approved)
       { gateway.http().complete_role_change_request(device_id, request_id, role, approved); },
       [&gateway](std::string_view device_id, std::string_view request_id, bool approved)
       { gateway.http().complete_global_setting_change_request(device_id, request_id, approved); },
       std::move(present_role),
       std::move(present_global_setting),
       std::move(cancel_device)});
}

ManagementUiCallbacks make_management_callbacks(const MacosManagementServices& services)
{
  if (!services.sign_in_startup_enabled || !services.set_sign_in_startup)
    throw std::invalid_argument("Native sign-in startup callbacks are required");
  ManagementUiCallbacks callbacks;
  add_client_callbacks(callbacks, services);
  add_obs_connection_callbacks(callbacks, services);
  add_streamlabs_connection_callbacks(callbacks, services);
  add_adapter_callbacks(callbacks, services);
  add_network_callbacks(callbacks, services);
  add_status_callbacks(callbacks, services);
  add_access_settings_callbacks(callbacks, services);
  add_endurance_callbacks(callbacks, services);
  add_audio_mix_callbacks(callbacks, services);
  callbacks.shutdown = [services] { services.shutdown.request_stop(); };
  add_audio_diagnostic_callbacks(callbacks, services);
  return callbacks;
}
} // namespace studio_remote::macos
