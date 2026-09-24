#pragma once

#include "adapter_gateway_controller.hpp"
#include "http_server.hpp"
#include "listener_gateway_controller.hpp"

#include "../../platform/linux/capture/linux_audio_diagnostic_player.hpp"
#include "../../platform/linux/capture/linux_audio_monitor.hpp"
#include "../../platform/linux/capture/linux_capture.hpp"
#include "../../platform/linux/credentials/linux_credential_store.hpp"
#include "../../platform/linux/platform_services/linux_autostart.hpp"
#include "../../platform/linux/platform_services/linux_config.hpp"
#include "../../platform/linux/platform_services/linux_network.hpp"
#include "../../platform/linux/platform_services/linux_obs_client.hpp"
#include "../../platform/linux/platform_services/linux_process.hpp"

#include <functional>
#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/credentials/pairing_manager.hpp>
#include <studio_remote/observability/endurance_event_log.hpp>

namespace studio_remote::linux
{
/// Non-owning view of the runtime objects assembled by the Linux composition root. It exists only
/// to hand the management and approval composition the same objects the root already owns; it is
/// not a service locator and must not grow behavior. The root keeps every referenced object alive
/// until the management UI and the HTTP approval handlers built from this view are cleared.
struct LinuxCompanionServices
{
  GatewayConfig& config;
  ConfigStore& config_store;
  LinuxCredentialStore& credentials;
  SignInStartup& sign_in_startup;
  AuditStore& audit;
  PairingManager& pairing;
  ObsClient& obs;
  GatewayRuntime& runtime;
  GatewayRuntime& teleprompter_runtime;
  teleprompter::IRepository& scripts;
  WebRtcHub& webrtc;
  CaptureManager& capture;
  LinuxAudioMonitor& audio_monitor;
  LinuxAudioDiagnosticPlayer& audio_diagnostics;
  AdapterGatewayController& adapter_gateways;
  GatewayHttpServer& http;
  ListenerGatewayController& network;
  AvahiServiceDiscovery& discovery;
  EnduranceEventLog& endurance_log;
  ShutdownSignal& shutdown;
};
} // namespace studio_remote::linux
