#include "linux_client_approvals.hpp"
#include "linux_command_line.hpp"
#include "linux_companion_services.hpp"
#include "linux_gateway_advertisement.hpp"
#include "linux_management_composition.hpp"
#include "obs_media_delivery.hpp"
#include "platform_companion_application.hpp"

#include "../../platform/linux/capture/linux_audio_mixer_roster.hpp"
#include "../../platform/linux/capture/linux_scene_item_properties.hpp"
#include "../../platform/linux/capture/linux_thumbnail_capture.hpp"
#include "../../platform/linux/capture/linux_thumbnail_encoder.hpp"
#include "../../platform/shared/teleprompter/session_device_source.hpp"
#include "../../platform/shared/teleprompter/sqlite_teleprompter_repository.hpp"
#include "../../ui/linux/linux_management_ui.hpp"
#include <studio_remote/teleprompter/adapter.hpp>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>

namespace studio_remote::linux
{
namespace
{
class UnavailableSystemHotkeyExecutor final : public IHotkeyExecutor
{
public:
  bool available() const noexcept override
  {
    return false;
  }

  void trigger(std::span<const HotkeyKey>) override
  {
    throw HotkeyExecutionError("interactive-desktop-unavailable",
                               "Linux desktop input injection is disabled");
  }
};

bool graphical_session_available()
{
  const auto present = [](const char* name)
  {
    const auto* value = std::getenv(name);
    return value && *value;
  };
  return present("WAYLAND_DISPLAY") || present("DISPLAY");
}

void apply_command_line_overrides(GatewayConfig& config, const CommandLine& command_line)
{
  if (command_line.name)
    config.name = *command_line.name;
  if (command_line.gateway_port)
    config.port = *command_line.gateway_port;
  if (command_line.obs_host)
  {
    config.obs_host = *command_line.obs_host;
    config.obs_connection_configured = true;
  }
  if (command_line.obs_port)
  {
    config.obs_port = *command_line.obs_port;
    config.obs_connection_configured = true;
  }
  const auto addresses = available_lan_ipv4_addresses();
  if (std::ranges::find(addresses, config.pairing_address) == addresses.end())
    config.pairing_address = addresses.empty() ? std::string{} : addresses.front();
}

CaptureManager make_capture_manager(ObsMediaDelivery& delivery, WebRtcHub& webrtc)
{
  return CaptureManager(
      [&delivery](std::string_view track, std::shared_ptr<const RawVideoFrame> frame)
      { delivery.receive_video(track, std::move(frame)); },
      [&delivery](std::string_view track, const json& health)
      { delivery.receive_video_health(track, health); },
      [&webrtc] { return webrtc.video_demand(); },
      [&delivery](std::uint32_t generation) { delivery.receive_capture_generation(generation); });
}

class LinuxCompanionApplication final : public PlatformCompanionApplication
{
public:
  int run(int argc, char** argv) override;

private:
  static int run_gateway(const CommandLine& command_line,
                         ConfigStore& config_store,
                         GatewayConfig& config,
                         LinuxCredentialStore& credentials,
                         EnduranceEventLog& endurance_log,
                         ShutdownSignal& shutdown);
};

int LinuxCompanionApplication::run(int argc, char** argv)
{
  // A disconnected local or network peer must not terminate the companion through SIGPIPE.
  std::signal(SIGPIPE, SIG_IGN);
  std::unique_ptr<EnduranceEventLog> endurance_log;
  try
  {
    // POSIX masks must precede every worker, including threads created by GLib/Secret Service.
    ShutdownSignal shutdown;
    const auto command_line = parse_command_line(argc, argv);
    if (command_line.help)
    {
      print_help(std::cout);
      return 0;
    }
    if (command_line.version)
    {
      std::cout << PROJECT_VERSION << '\n';
      return 0;
    }
    if (command_line.open_management && activate_existing_management_ui())
      return 0;

    ConfigStore config_store(command_line.data_directory);
    auto config = config_store.load();
    if (command_line.set_sign_in_startup)
    {
      SignInStartup().set_enabled(*command_line.set_sign_in_startup);
      return 0;
    }
    SingleInstance single_instance(config.data_directory);
    if (!single_instance.owns_instance())
    {
      std::cerr << "PaneLive Companion is already running for this user.\n";
      return 0;
    }
    apply_command_line_overrides(config, command_line);
    LinuxCredentialStore credentials(config.instance_id);
    if (command_line.obs_password)
    {
      credentials.put("obs-password", *command_line.obs_password);
      config.obs_connection_configured = true;
    }
    config.obs_password = credentials.get("obs-password");
    config_store.save(config);

    endurance_log = std::make_unique<EnduranceEventLog>(config.data_directory / "endurance-logs",
                                                        PROJECT_VERSION);
    endurance_log->record("process",
                          "started",
                          {{"version", PROJECT_VERSION},
                           {"platform", "linux"},
                           {"providerIds", {"obs", "companion-teleprompter"}}});
    const int status =
        run_gateway(command_line, config_store, config, credentials, *endurance_log, shutdown);
    endurance_log->record("process", "stopped");
    endurance_log->stop();
    return status;
  }
  catch (const std::exception& error)
  {
    if (endurance_log)
    {
      endurance_log->record("process", "fatalException", {{"message", error.what()}});
      endurance_log->stop();
    }
    std::cerr << "Gateway failed: " << error.what() << '\n';
    return 1;
  }
}

// Assembles the runtime in dependency order on the stack so destruction reverses construction.
// Objects that hand callbacks to others are declared before the receivers and cleared explicitly
// before returning, so no callback can observe a partially destroyed runtime.
int LinuxCompanionApplication::run_gateway(const CommandLine& command_line,
                                           ConfigStore& config_store,
                                           GatewayConfig& config,
                                           LinuxCredentialStore& credentials,
                                           EnduranceEventLog& endurance_log,
                                           ShutdownSignal& shutdown)
{
  SignInStartup sign_in_startup;
  AuditStore audit(config.data_directory / "gateway.db", {}, &endurance_log);
  teleprompter::SqliteRepository scripts(
      config.data_directory / "gateway.db",
      teleprompter::session_device_source(audit),
      [&credentials] { return credentials.uuid_v4(); },
      []
      {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count());
      },
      []
      {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count());
      });
  if (config.enforce_single_operator_client_mode)
    audit.enforce_single_operator();
  const GatewayIdentity identity{config.instance_id, config.name, config.certificate.fingerprint};
  PairingManager pairing(identity, audit, credentials, {"obs", "companion-teleprompter"});
  UnavailableSystemHotkeyExecutor system_hotkeys;
  ObsClient obs(config.obs_host, config.obs_port, config.obs_password);
  obs.on_diagnostic_event(
      [&endurance_log](std::string_view event, json details)
      {
        if (endurance_log.enabled())
          endurance_log.record("obsWebSocket", event, std::move(details));
      });
  LinuxAudioMixerRoster audio_mixer_roster;
  LinuxSceneItemProperties scene_item_properties;
  LinuxThumbnailCapture thumbnail_capture;
  LinuxThumbnailEncoder thumbnail_encoder;
  teleprompter::Adapter teleprompter_backend;
  GatewayRuntime teleprompter_runtime(identity, teleprompter_backend, audit);
  // Retain the common session media owner without starting a media pipeline.
  WebRtcHub teleprompter_webrtc(teleprompter_runtime.state());
  GatewayRuntime runtime(identity,
                         obs,
                         audit,
                         &thumbnail_capture,
                         &thumbnail_encoder,
                         &audio_mixer_roster,
                         &scene_item_properties);
  WebRtcHub webrtc(runtime.state());
  LinuxAudioDiagnosticPlayer audio_diagnostics;
  ObsMediaDelivery media_delivery(
      runtime.state(),
      webrtc,
      [&audio_diagnostics](const AudioMonitorStatus& status)
      { audio_diagnostics.set_monitor_status(status); },
      &endurance_log);
  CaptureManager capture = make_capture_manager(media_delivery, webrtc);
  LinuxAudioMonitor audio_monitor(
      [&audio_diagnostics, &webrtc](SharedAudioMonitorFrame frame)
      {
        audio_diagnostics.push(frame, AudioDiagnosticTap::pre_opus_ipc);
        webrtc.push_audio_monitor(std::move(frame));
      },
      [&media_delivery](const AudioMonitorStatus& status)
      { media_delivery.receive_audio_status(status); });
  apply_audio_mix_configuration(config, audio_monitor, runtime);
  webrtc.set_post_opus_diagnostic_handler(
      [&audio_diagnostics](SharedAudioMonitorFrame frame)
      { audio_diagnostics.push(std::move(frame), AudioDiagnosticTap::post_opus); });
  AdapterGatewayController adapter_gateways(
      {{"companion-teleprompter",
        [&teleprompter_runtime] { return teleprompter_runtime.running(); },
        [&teleprompter_runtime] { teleprompter_runtime.start(); },
        [&teleprompter_runtime] { teleprompter_runtime.stop(); },
        [] {},
        [] {}},
       {"obs",
        [&runtime] { return runtime.running(); },
        [&runtime] { runtime.start(); },
        [&runtime] { runtime.stop(); },
        [&webrtc, &audio_monitor, &capture]
        {
          webrtc.start();
          audio_monitor.start();
          capture.start();
        },
        [&webrtc, &audio_monitor, &capture]
        {
          capture.stop();
          audio_monitor.stop();
          webrtc.stop();
        }}});
  HotkeyService hotkeys(
      system_hotkeys, {{"companion-teleprompter", &teleprompter_backend}, {"obs", &obs}}, audit);
  const auto make_http_config = [&config, &identity]
  {
    return HttpConfig{config.port,
                      listener_specification(config.port, config.allow_non_private_interfaces),
                      config.certificate.server_pem_path,
                      config.certificate.private_key_path,
                      std::filesystem::absolute(config.data_directory / "civetweb-error.log"),
                      identity};
  };
  GatewayHttpServer http(make_http_config(),
                         {{"companion-teleprompter", &teleprompter_runtime, &teleprompter_webrtc},
                          {"obs", &runtime, &webrtc}},
                         pairing,
                         hotkeys,
                         &endurance_log,
                         &scripts);
  media_delivery.on_capabilities_changed([&http] { http.publish_capabilities("obs"); });
  media_delivery.on_capture_generation_changed(
      [&http](std::uint32_t generation) { http.reinitialize_media_sessions("obs", generation); });
  webrtc.set_audio_monitor_status(audio_monitor.status());
  audio_diagnostics.set_monitor_status(audio_monitor.status());
  audit.on_roles_changed([&http](const std::vector<DeviceRoleChange>& changes)
                         { http.apply_role_changes(changes); });
  // As on Windows, an OBS exit or connection loss never ends the companion process: the OBS
  // adapter reconnects on its own and the helper IPC re-attaches when OBS returns.
  AvahiServiceDiscovery discovery;
  ListenerGatewayController network(
      {[](std::uint16_t port)
       {
         const bool available = listener_port_available(port);
         return ListenerPortAvailability{
             available,
             0,
             available ? ""
                       : "HTTPS/WSS listener port " + std::to_string(port) + " is already in use"};
       },
       [&config](std::uint16_t port)
       { return listener_specification(port, config.allow_non_private_interfaces); },
       [&http] { return http.running(); },
       [&discovery] { discovery.stop(); },
       [&http] { http.stop(); },
       [&http, &make_http_config](std::uint16_t port, const std::string& listeners)
       {
         auto settings = make_http_config();
         settings.port = port;
         settings.listening_ports = listeners;
         http.configure(settings);
       },
       [&http] { http.start(); },
       [&discovery, &config](std::uint16_t port)
       {
         auto advertised = config;
         advertised.port = port;
         discovery.advertise(dns_sd_service_record(advertised));
       },
       [](std::string_view error)
       { std::cerr << "DNS-SD advertisement failed: " << error << '\n'; }},
      listener_specification(config.port, config.allow_non_private_interfaces));
  adapter_gateways.start("companion-teleprompter");
  adapter_gateways.start("obs");
  try
  {
    network.start(config.port);
  }
  catch (const std::exception& error)
  {
    std::cerr << "Companion network recovery required: " << error.what() << '\n';
  }

  if (command_line.pairing_role)
  {
    const auto payload = pairing.open(pairing_url(config), *command_line.pairing_role);
    std::cout << payload.dump() << '\n';
  }
  std::cout << "PaneLive Companion '" << config.name << "' "
            << (http.running() ? "is listening on port " + std::to_string(config.port)
                               : "is running in network recovery mode")
            << ".\n";

  const LinuxCompanionServices services{
      config,  config_store, credentials,   sign_in_startup,      audit,
      pairing, obs,          runtime,       teleprompter_runtime, scripts,
      webrtc,  capture,      audio_monitor, audio_diagnostics,    adapter_gateways,
      http,    network,      discovery,     endurance_log,        shutdown};
  if (graphical_session_available())
  {
    auto approvals = linux_client_approval_callbacks(services);
    LinuxManagementUi management_ui(
        config.name,
        PROJECT_VERSION,
        config.obs_connection_configured,
        !http.running(),
        command_line.open_management,
        config.management_interface_mode,
        [&config, &config_store](ManagementInterfaceMode mode)
        {
          auto updated = config;
          updated.management_interface_mode = mode;
          config_store.save(updated);
          config = std::move(updated);
        },
        make_management_callbacks(services),
        shutdown,
        approvals.decide_role,
        approvals.decide_global_setting);
    pairing.on_closed(management_ui.pairing_closed_handler());
    auto requesters = management_ui.approval_requesters();
    http.on_client_removed(requesters.cancel_device);
    approvals = linux_client_approval_callbacks(services, std::move(requesters));
    http.on_role_change_requested(approvals.request_role);
    http.on_global_setting_change_requested(approvals.request_global_setting);
    static_cast<void>(management_ui.run());
    http.on_role_change_requested({});
    http.on_client_removed({});
    http.on_global_setting_change_requested({});
    pairing.on_closed({});
  }
  else
  {
    while (!shutdown.wait_for(std::chrono::milliseconds(250)))
    {
    }
  }

  audit.on_roles_changed({});
  pairing.on_closed({});
  adapter_gateways.stop("obs");
  adapter_gateways.stop("companion-teleprompter");
  http.stop();
  webrtc.set_post_opus_diagnostic_enabled(false);
  audio_diagnostics.stop();
  discovery.stop();
  return 0;
}
} // namespace
} // namespace studio_remote::linux

namespace studio_remote
{
std::unique_ptr<PlatformCompanionApplication> create_platform_companion_application()
{
  return std::make_unique<linux::LinuxCompanionApplication>();
}
} // namespace studio_remote
