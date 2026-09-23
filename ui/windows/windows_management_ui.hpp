#pragma once

#include "windows_audio_diagnostics.hpp"

#include "../../platform/windows/platform_services/windows_obs_connection.hpp"
#include "../../platform/windows/platform_services/windows_port_diagnostics.hpp"
#include <studio_remote/ui/management_adapter_rows.hpp>
#include <studio_remote/ui/management_clients.hpp>

#include <studio_remote/events/client_identification.hpp>
#include <studio_remote/media/audio_monitor_mix_settings.hpp>
#include <studio_remote/observability/endurance_event_log.hpp>
#include <studio_remote/teleprompter/repository.hpp>
#include <studio_remote/ui/client_approval_request.hpp>
#include <studio_remote/ui/management_approval_dispatcher.hpp>
#include <studio_remote/ui/management_interface.hpp>
#include <studio_remote/ui/management_tally.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{
struct PairingRequest
{
  std::string payload;
  std::chrono::system_clock::time_point expires;
};

struct ProductionInstallLocations
{
  std::string configured_vmix_directory;
  std::string detected_vmix_directory;
  std::string vmix_ndi_runtime_directory;
  std::string obs_directory;
};

struct GatewayNetworkStatus
{
  std::string address;
  std::vector<std::string> available_addresses;
  std::uint16_t port = 7443;
  bool listening = false;
  std::string warning;
};

struct GatewayNetworkSettings
{
  std::string address;
  std::uint16_t port = 7443;
};

/// WinUI 3 host for the temporary management surface and notification-area integration.
/// All application behavior is supplied as typed callbacks; the GUI owns presentation only.
class WindowsManagementUi
{
public:
  using ClientProvider = std::function<std::vector<ManagementClient>()>;
  using PairingHandler = std::function<PairingRequest()>;
  using SetOperatorHandler = std::function<void(std::string_view)>;
  using SetViewerHandler = std::function<void(std::string_view)>;
  using ForgetClientHandler = std::function<void(std::string_view)>;
  using ObsConnectionProvider = std::function<ObsConnectionStatus()>;
  using VmixConnectionProvider = std::function<VmixConnectionStatus()>;
  using StatusProvider = std::function<ManagementStatusSnapshot()>;
  using AdapterGatewayStatusProvider = std::function<std::vector<AdapterGatewayStatus>()>;
  using AdapterGatewayActionHandler = std::function<void(std::string_view, AdapterGatewayAction)>;
  using ApplyObsConnectionHandler = std::function<void(const ObsConnectionSettings&)>;
  using ApplyVmixConnectionHandler = std::function<void(const VmixConnectionSettings&)>;
  using GatewayNetworkStatusProvider = std::function<GatewayNetworkStatus()>;
  using ApplyGatewayNetworkHandler = std::function<void(const GatewayNetworkSettings&)>;
  using StartupPortDiagnosticsProvider = std::function<StartupPortDiagnostics()>;
  using MixSettingsProvider = std::function<AudioMonitorMixSettings()>;
  using ApplyMixSettingsHandler = std::function<void(const AudioMonitorMixSettings&)>;
  using InstallLocationsProvider = std::function<ProductionInstallLocations()>;
  using ApplyVmixDirectoryHandler = std::function<void(std::string_view)>;
  using SingleOperatorModeProvider = std::function<bool()>;
  using SetSingleOperatorModeHandler = std::function<void(bool)>;
  using SignInStartupProvider = std::function<bool()>;
  using SetSignInStartupHandler = std::function<void(bool)>;
  using EnduranceLogStatusProvider = std::function<EnduranceLogStatus()>;
  using SetEnduranceLoggingHandler = std::function<void(bool)>;
  using RoleChangeDecisionHandler = std::function<void(const RoleChangeRequest&, bool approved)>;
  using GlobalSettingChangeDecisionHandler =
      std::function<void(const GlobalSettingChangeRequest&, bool approved)>;
  using ShutdownHandler = std::function<void()>;
  using InterfaceModeChangedHandler = std::function<void(ManagementInterfaceMode)>;

  WindowsManagementUi(
      std::string gateway_name,
      std::string build_version,
      bool obs_connection_configured,
      bool network_recovery_required,
      ManagementInterfaceMode interface_mode,
      InterfaceModeChangedHandler interface_mode_changed,
      ClientProvider clients,
      PairingHandler pairing,
      SetOperatorHandler set_operator,
      SetViewerHandler set_viewer,
      ForgetClientHandler forget_client,
      ObsConnectionProvider obs_connection,
      ApplyObsConnectionHandler apply_obs_connection,
      VmixConnectionProvider vmix_connection,
      ApplyVmixConnectionHandler apply_vmix_connection,
      GatewayNetworkStatusProvider gateway_network_status,
      ApplyGatewayNetworkHandler apply_gateway_network,
      StartupPortDiagnosticsProvider startup_port_diagnostics,
      StatusProvider status,
      AdapterGatewayStatusProvider adapter_gateway_status,
      AdapterGatewayActionHandler adapter_gateway_action,
      AudioDiagnosticsCallbacks audio_diagnostics,
      MixSettingsProvider mix_settings,
      ApplyMixSettingsHandler apply_mix_settings,
      InstallLocationsProvider install_locations,
      ApplyVmixDirectoryHandler apply_vmix_directory,
      SingleOperatorModeProvider single_operator_mode,
      SetSingleOperatorModeHandler set_single_operator_mode,
      SignInStartupProvider sign_in_startup,
      SetSignInStartupHandler set_sign_in_startup,
      EnduranceLogStatusProvider endurance_log_status,
      SetEnduranceLoggingHandler set_endurance_logging,
      std::function<void()> clear_debug_logs,
      RoleChangeDecisionHandler role_change_decision,
      GlobalSettingChangeDecisionHandler global_setting_change_decision,
      ShutdownHandler shutdown,
      std::function<void(std::string_view, ClientIdentificationOptions)> identify_client,
      std::function<void(ClientIdentificationOptions)> identify_all_clients,
      RemoteTallyManagementCallbacks remote_tally,
      teleprompter::IRepository* teleprompter = nullptr);
  ~WindowsManagementUi();

  WindowsManagementUi(const WindowsManagementUi&) = delete;
  WindowsManagementUi& operator=(const WindowsManagementUi&) = delete;

  void show_management();
  void show_pairing(PairingRequest request);
  void hide_pairing();
  // Safe to retain across frontend destruction; copied invocations are joined before teardown.
  std::function<void()> pairing_closed_handler() const;
  ManagementApprovalRequesters approval_requesters() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::windows
