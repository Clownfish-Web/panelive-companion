#pragma once

#include "../../platform/macos/credentials/macos_credential_store.hpp"
#include "../../platform/macos/platform_services/macos_hotkey_executor.hpp"
#include "../../platform/macos/platform_services/macos_sign_in_startup.hpp"
#include "../../platform/shared/teleprompter/sqlite_teleprompter_repository.hpp"
#include "../../ui/macos/management_worker.hpp"
#include "macos_command_line.hpp"
#include "macos_management_composition.hpp"
#include <studio_remote/observability/endurance_process_log.hpp>
#include <studio_remote/ui/management_approval_dispatcher.hpp>

namespace studio_remote::macos
{
/// Bootstrap and teardown run outside AppKit. After bootstrap, ManagementWorker exclusively owns
/// configuration mutations and service lifecycle. The root invalidates its UI before destruction.
class MacosApplicationServices final
{
public:
  MacosApplicationServices(const CommandLine& command_line,
                           MacosShutdownSignal& shutdown,
                           std::stop_token cancellation,
                           EnduranceProcessLog& process_log);
  ~MacosApplicationServices();
  MacosApplicationServices(const MacosApplicationServices&) = delete;
  MacosApplicationServices& operator=(const MacosApplicationServices&) = delete;

  ManagementWorker& management() noexcept;
  bool show_management_on_start() const noexcept;
  ClientApprovalCallbacks client_approval_callbacks(ManagementApprovalRequesters requesters = {});
  void connect_client_approval_requests(ManagementApprovalRequesters requesters);
  void disconnect_client_approval_requests();
  /// Root-owned presentation callback; clear before destroying the UI. Copied callbacks may run
  /// after clearing, so their native targets must have independent lifetime protection.
  void on_pairing_closed(std::function<void()> handler);
  /// Normal teardown worker only, after native UI invalidation and before destruction.
  void prepare_normal_shutdown();

private:
  void create_management(const CommandLine& command_line,
                         MacosShutdownSignal& shutdown,
                         bool network_recovery);

  ConfigStore store_;
  GatewayConfig config_;
  MacosCredentialStore credentials_;
  // Process-owned recorder outlives this assembly, including constructor unwinding.
  EnduranceEventLog* log_ = nullptr;
  // Deferred until configuration persistence succeeds. All borrowers below die first.
  std::unique_ptr<AuditStore> audit_;
  std::unique_ptr<teleprompter::SqliteRepository> scripts_;
  MacosHotkeyExecutor hotkeys_;
  MacosSignInStartup sign_in_;
  std::unique_ptr<MacosGatewayComposition> gateway_;
  ManagementStatusSnapshotBuilder status_snapshot_builder_;
  std::unique_ptr<ManagementWorker> management_;
  bool show_management_ = false;
};
} // namespace studio_remote::macos
