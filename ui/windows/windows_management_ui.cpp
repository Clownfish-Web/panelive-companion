#include "windows_management_ui.hpp"

#include "windows_tray_icon.hpp"
#include "winui_management_window.hpp"

#include <studio_remote/ui/management_client_notifications.hpp>

#include "../../platform/windows/platform_services/windows_management_view_adapter.hpp"

#include <MddBootstrap.h>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/base.h>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace
{
constexpr std::uint32_t windows_app_sdk_major_minor = 0x00020004;

class WinUiRuntimeScope
{
public:
  WinUiRuntimeScope()
  {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    const PACKAGE_VERSION minimum_version{};
    const auto result = MddBootstrapInitialize2(
        windows_app_sdk_major_minor, L"", minimum_version, MddBootstrapInitializeOptions_None);
    if (FAILED(result))
    {
      winrt::uninit_apartment();
      throw std::runtime_error("Windows App Runtime 2.4 is unavailable");
    }
  }
  ~WinUiRuntimeScope()
  {
    MddBootstrapShutdown();
    winrt::uninit_apartment();
  }
  WinUiRuntimeScope(const WinUiRuntimeScope&) = delete;
  WinUiRuntimeScope& operator=(const WinUiRuntimeScope&) = delete;
};

ManagementEnduranceStatus make_endurance_status(const EnduranceLogStatus& status)
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

ManagementAdvancedSettings make_advanced_settings(const AudioMonitorMixSettings& mix,
                                                  const ProductionInstallLocations& locations)
{
  return {mix.automatic_headroom,
          mix.attenuation_per_additional_source_db,
          mix.activate_audio_source_audition_bus,
          locations.configured_vmix_directory,
          locations.detected_vmix_directory,
          locations.vmix_ndi_runtime_directory,
          locations.obs_directory};
}

ManagementNetworkSettings make_network_settings(const GatewayNetworkStatus& status)
{
  return {
      status.address, status.available_addresses, status.port, status.listening, status.warning};
}

ManagementPairing make_pairing(PairingRequest request)
{
  return {std::move(request.payload),
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::seconds>(request.expires.time_since_epoch())
                  .count())};
}
} // namespace

class WindowsManagementUi::Impl
{
public:
  Impl(std::string gateway_name,
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
       teleprompter::IRepository* teleprompter)
      : gateway_name_(std::move(gateway_name)), build_version_(std::move(build_version)),
        interface_mode_(interface_mode),
        initial_page_(initial_management_page(
            interface_mode, obs_connection_configured, network_recovery_required)),
        startup_port_diagnostics_(std::move(startup_port_diagnostics))
  {
    callbacks_.clients = [this, clients = std::move(clients)]
    {
      auto rows = clients ? make_management_client_rows(clients(), std::chrono::system_clock::now())
                          : std::vector<ManagementClientRow>{};
      // Observe only successful snapshots. The initial refresh establishes a silent baseline
      // before the tray is created; failed providers never look like a mass disconnect.
      const auto notice = client_notifications_.observe(rows);
      if (notice && tray_)
        tray_->show_notification(notice->title, notice->body);
      return rows;
    };
    callbacks_.pair_client = [pairing = std::move(pairing)]
    { return pairing ? make_pairing(pairing()) : ManagementPairing{}; };
    callbacks_.set_operator = std::move(set_operator);
    callbacks_.set_viewer = std::move(set_viewer);
    callbacks_.forget_client = std::move(forget_client);
    callbacks_.identify_client = std::move(identify_client);
    callbacks_.identify_all_clients = std::move(identify_all_clients);
    callbacks_.teleprompter = teleprompter;
    callbacks_.remote_tally = std::move(remote_tally);
    callbacks_.obs_connection = [provider = std::move(obs_connection)]
    { return provider ? make_management_obs_connection(provider()) : ManagementObsConnection{}; };
    callbacks_.apply_obs_connection =
        [handler = std::move(apply_obs_connection)](const ManagementObsConnectionRequest& request)
    {
      if (handler)
        handler({request.host, request.port, request.password});
    };
    callbacks_.vmix_connection = [provider = std::move(vmix_connection)]
    { return provider ? make_management_vmix_connection(provider()) : ManagementVmixConnection{}; };
    callbacks_.apply_vmix_connection =
        [handler = std::move(apply_vmix_connection)](const ManagementVmixConnectionRequest& request)
    {
      if (handler)
      {
        handler({request.host,
                 request.http_port,
                 request.username,
                 request.password,
                 request.program_output,
                 request.preview_output,
                 request.program_port,
                 request.preview_port,
                 request.latency_ms});
      }
    };
    callbacks_.network_settings = [provider = std::move(gateway_network_status)]
    { return provider ? make_network_settings(provider()) : ManagementNetworkSettings{}; };
    callbacks_.apply_network_settings = [handler = std::move(apply_gateway_network)](
                                            const ManagementNetworkSettingsRequest& request)
    {
      if (handler)
        handler({request.address, request.port});
    };
    callbacks_.status_feeds = [provider = std::move(status)]
    {
      return provider ? make_management_status_rows(provider())
                      : std::vector<ManagementStatusRow>{};
    };
    callbacks_.adapters = [provider = std::move(adapter_gateway_status)]
    {
      return provider ? make_management_adapter_rows(provider())
                      : std::vector<ManagementAdapterRow>{};
    };
    callbacks_.start_adapter = [handler = adapter_gateway_action](std::string_view id)
    {
      if (handler)
        handler(id, AdapterGatewayAction::start);
    };
    callbacks_.stop_adapter = [handler = adapter_gateway_action](std::string_view id)
    {
      if (handler)
        handler(id, AdapterGatewayAction::stop);
    };
    callbacks_.restart_adapter = [handler = std::move(adapter_gateway_action)](std::string_view id)
    {
      if (handler)
        handler(id, AdapterGatewayAction::restart);
    };
    callbacks_.audio_outputs = [provider = audio_diagnostics.devices]
    {
      return provider ? make_management_audio_outputs(provider())
                      : std::vector<ManagementAudioOutput>{};
    };
    callbacks_.audio_status = [status = audio_diagnostics.status,
                               debug_active = audio_diagnostics.debug_log_active,
                               debug_available = audio_diagnostics.debug_log_available]
    {
      return status ? make_management_audio_status(status(),
                                                   debug_active && debug_active(),
                                                   debug_available && debug_available())
                    : ManagementAudioDiagnosticStatus{};
    };
    callbacks_.start_audio =
        [handler = audio_diagnostics.start](std::string_view id, ManagementAudioDiagnosticTap tap)
    {
      if (handler)
      {
        handler(id,
                tap == ManagementAudioDiagnosticTap::pre_opus ? AudioDiagnosticTap::pre_opus_ipc
                                                              : AudioDiagnosticTap::post_opus);
      }
    };
    callbacks_.stop_audio = std::move(audio_diagnostics.stop);
    callbacks_.set_audio_gain = std::move(audio_diagnostics.set_gain_db);
    callbacks_.begin_audio_debug_log = std::move(audio_diagnostics.begin_debug_log);
    callbacks_.complete_audio_debug_log = std::move(audio_diagnostics.complete_debug_log);
    callbacks_.audio_debug_report = std::move(audio_diagnostics.debug_log_report);
    callbacks_.advanced_settings =
        [mix_settings = std::move(mix_settings), install_locations = std::move(install_locations)]
    {
      return make_advanced_settings(mix_settings ? mix_settings() : AudioMonitorMixSettings{},
                                    install_locations ? install_locations()
                                                      : ProductionInstallLocations{});
    };
    callbacks_.apply_audio_mix =
        [handler = std::move(apply_mix_settings)](const ManagementAudioMixSettingsRequest& request)
    {
      if (handler)
      {
        handler({request.automatic_headroom,
                 request.headroom_per_additional_source_db,
                 request.audio_source_audition_bus});
      }
    };
    callbacks_.apply_vmix_directory = std::move(apply_vmix_directory);
    callbacks_.basic_settings = [single_operator_mode = std::move(single_operator_mode),
                                 sign_in_startup = std::move(sign_in_startup)]
    {
      return ManagementBasicSettings{single_operator_mode && single_operator_mode(),
                                     sign_in_startup && sign_in_startup()};
    };
    callbacks_.set_single_operator_mode = std::move(set_single_operator_mode);
    callbacks_.set_sign_in_startup = std::move(set_sign_in_startup);
    callbacks_.endurance_status = [provider = std::move(endurance_log_status)]
    { return provider ? make_endurance_status(provider()) : ManagementEnduranceStatus{}; };
    callbacks_.set_endurance_logging = std::move(set_endurance_logging);
    callbacks_.clear_debug_logs = std::move(clear_debug_logs);
    callbacks_.shutdown = std::move(shutdown);
    mode_changed_ = std::move(interface_mode_changed);
    approvals_ = std::make_unique<ManagementApprovalDispatcher>(ManagementApprovalDispatchActions{
        [this](auto action) { return dispatch(std::move(action)); },
        [](auto action)
        {
          action();
          return true;
        },
        [this](ManagementRoleChangeRequest request, ManagementApprovalDecision decision)
        {
          window_->request_role_change(std::move(request), std::move(decision));
          return true;
        },
        [this](ManagementGlobalSettingChangeRequest request, ManagementApprovalDecision decision)
        {
          window_->request_global_setting_change(std::move(request), std::move(decision));
          return true;
        },
        std::move(role_change_decision),
        std::move(global_setting_change_decision),
        [this](std::string_view error) noexcept
        {
          try
          {
            if (!dispatch(
                    [this, message = std::string(error)]
                    {
                      try
                      {
                        window_->report_approval_error(message);
                      }
                      catch (...)
                      {
                        OutputDebugStringW(L"Could not present a client approval error.\n");
                      }
                    }))
              OutputDebugStringW(L"Could not report a client approval failure.\n");
          }
          catch (...)
          {
            OutputDebugStringW(L"Could not report a client approval failure.\n");
          }
        },
        [this](std::string_view device_id, std::string_view request_id)
        { window_->cancel_role_change(device_id, request_id); },
        [this](std::string_view device_id, std::string_view request_id)
        { window_->cancel_global_setting_change(device_id, request_id); }});
    pairing_close_->hide = [this] { hide_pairing(); };
    thread_ = std::thread([this] { thread_main(); });
    std::unique_lock lock(ready_mutex_);
    ready_cv_.wait(lock, [this] { return ready_; });
    if (startup_failure_)
    {
      lock.unlock();
      thread_.join();
      std::rethrow_exception(startup_failure_);
    }
  }

  ~Impl()
  {
    {
      std::lock_guard lock(pairing_close_->mutex);
      pairing_close_->hide = {};
    }
    dispatch(
        [this]
        {
          approvals_->close();
          tray_.reset();
          if (window_)
            window_->close();
        });
    if (thread_.joinable())
      thread_.join();
    approvals_->close();
  }

  bool dispatch(std::function<void()> action)
  {
    std::lock_guard lock(ready_mutex_);
    if (!dispatcher_ || stopping_)
      return false;
    return dispatcher_.TryEnqueue(std::move(action));
  }

  void show_management()
  {
    dispatch([this] { window_->activate(); });
  }

  void show_pairing(PairingRequest request)
  {
    auto pairing = make_pairing(std::move(request));
    dispatch([this, pairing = std::move(pairing)]() mutable
             { window_->show_pairing(std::move(pairing)); });
  }

  void hide_pairing()
  {
    dispatch([this] { window_->clear_pairing(); });
  }

  std::function<void()> pairing_closed_handler() const
  {
    return [target = pairing_close_]
    {
      try
      {
        std::lock_guard lock(target->mutex);
        if (target->hide)
          target->hide();
      }
      catch (...)
      {
        OutputDebugStringW(L"Could not close pairing presentation.\n");
      }
    };
  }

  ManagementApprovalRequesters approval_requesters() const
  {
    return approvals_->requesters();
  }

private:
  void publish_startup_result()
  {
    {
      std::lock_guard lock(ready_mutex_);
      ready_ = true;
    }
    ready_cv_.notify_all();
  }

  void initialize_presentation()
  {
    try
    {
      dispatcher_ = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
      controller_ = std::make_unique<ManagementUiController>(
          interface_mode_, initial_page_, std::move(mode_changed_), std::move(callbacks_));
      controller_->refresh();
      window_ =
          std::make_unique<WinUiManagementWindow>(gateway_name_, build_version_, *controller_);
      tray_ = std::make_unique<WindowsTrayIcon>(
          WindowsTrayActions{[this] { window_->activate(); },
                             [this]
                             {
                               controller_->pair_client();
                               if (controller_->state().pairing)
                                 window_->show_pairing(*controller_->state().pairing);
                             },
                             [this](ManagementInterfaceMode mode)
                             {
                               controller_->set_interface_mode(mode);
                               window_->refresh();
                             },
                             [this] { return controller_->state().interface_mode; },
                             [this] { controller_->shutdown(); }});
      if (startup_port_diagnostics_)
      {
        const auto diagnostics = startup_port_diagnostics_();
        if (!diagnostics.issues.empty())
        {
          controller_->report_error(diagnostics.message);
          window_->activate();
        }
      }
    }
    catch (...)
    {
      startup_failure_ = std::current_exception();
    }
    publish_startup_result();
    if (startup_failure_)
      xaml::Application::Current().Exit();
  }

  void clear_presentation()
  {
    {
      std::lock_guard lock(ready_mutex_);
      stopping_ = true;
      dispatcher_ = nullptr;
    }
    approvals_->close();
    tray_.reset();
    window_.reset();
    controller_.reset();
  }

  void thread_main() noexcept
  {
    try
    {
      WinUiRuntimeScope runtime;
      try
      {
        run_winui_application([this] { initialize_presentation(); });
      }
      catch (...)
      {
        std::lock_guard lock(ready_mutex_);
        if (!ready_)
          startup_failure_ = std::current_exception();
        else
          OutputDebugStringW(L"WinUI management message loop failed.\n");
      }
      clear_presentation();
    }
    catch (...)
    {
      std::lock_guard lock(ready_mutex_);
      if (!ready_)
        startup_failure_ = std::current_exception();
      else
        OutputDebugStringW(L"WinUI management runtime failed.\n");
      stopping_ = true;
    }
    publish_startup_result();
  }
  std::string gateway_name_;
  std::string build_version_;
  ManagementInterfaceMode interface_mode_ = ManagementInterfaceMode::basic;
  ManagementPage initial_page_ = ManagementPage::clients;
  ManagementUiCallbacks callbacks_;
  InterfaceModeChangedHandler mode_changed_;
  StartupPortDiagnosticsProvider startup_port_diagnostics_;
  std::unique_ptr<ManagementApprovalDispatcher> approvals_;
  std::unique_ptr<ManagementUiController> controller_;
  std::unique_ptr<WinUiManagementWindow> window_;
  std::unique_ptr<WindowsTrayIcon> tray_;
  // Accessed exclusively on the frontend thread, including while the window is hidden.
  ManagementClientNotifications client_notifications_;
  winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
  std::mutex ready_mutex_;
  std::condition_variable ready_cv_;
  std::thread thread_;
  bool ready_ = false;
  bool stopping_ = false;
  std::exception_ptr startup_failure_;
  struct PairingCloseTarget
  {
    std::mutex mutex;
    std::function<void()> hide;
  };
  // The retained callback owns only this gate; the frontend still owns all UI resources.
  std::shared_ptr<PairingCloseTarget> pairing_close_ = std::make_shared<PairingCloseTarget>();
};

WindowsManagementUi::WindowsManagementUi(
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
    teleprompter::IRepository* teleprompter)
    : impl_(std::make_unique<Impl>(std::move(gateway_name),
                                   std::move(build_version),
                                   obs_connection_configured,
                                   network_recovery_required,
                                   interface_mode,
                                   std::move(interface_mode_changed),
                                   std::move(clients),
                                   std::move(pairing),
                                   std::move(set_operator),
                                   std::move(set_viewer),
                                   std::move(forget_client),
                                   std::move(obs_connection),
                                   std::move(apply_obs_connection),
                                   std::move(vmix_connection),
                                   std::move(apply_vmix_connection),
                                   std::move(gateway_network_status),
                                   std::move(apply_gateway_network),
                                   std::move(startup_port_diagnostics),
                                   std::move(status),
                                   std::move(adapter_gateway_status),
                                   std::move(adapter_gateway_action),
                                   std::move(audio_diagnostics),
                                   std::move(mix_settings),
                                   std::move(apply_mix_settings),
                                   std::move(install_locations),
                                   std::move(apply_vmix_directory),
                                   std::move(single_operator_mode),
                                   std::move(set_single_operator_mode),
                                   std::move(sign_in_startup),
                                   std::move(set_sign_in_startup),
                                   std::move(endurance_log_status),
                                   std::move(set_endurance_logging),
                                   std::move(clear_debug_logs),
                                   std::move(role_change_decision),
                                   std::move(global_setting_change_decision),
                                   std::move(shutdown),
                                   std::move(identify_client),
                                   std::move(identify_all_clients),
                                   std::move(remote_tally),
                                   teleprompter))
{
}

WindowsManagementUi::~WindowsManagementUi() = default;

void WindowsManagementUi::show_management()
{
  impl_->show_management();
}

void WindowsManagementUi::show_pairing(PairingRequest request)
{
  impl_->show_pairing(std::move(request));
}

void WindowsManagementUi::hide_pairing()
{
  impl_->hide_pairing();
}

std::function<void()> WindowsManagementUi::pairing_closed_handler() const
{
  return impl_->pairing_closed_handler();
}

ManagementApprovalRequesters WindowsManagementUi::approval_requesters() const
{
  return impl_->approval_requesters();
}
} // namespace studio_remote::windows
