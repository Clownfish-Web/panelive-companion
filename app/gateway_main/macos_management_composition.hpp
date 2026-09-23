#pragma once

#include "../../platform/macos/platform_services/macos_shutdown_signal.hpp"
#include "client_approval_callbacks.hpp"
#include "macos_gateway_composition.hpp"

#include <functional>
#include <string>
#include <studio_remote/ui/management_controller.hpp>
#include <studio_remote/ui/management_status_snapshot_builder.hpp>

namespace studio_remote::macos
{
/// Non-owning configuration-worker view, not a service locator. The root keeps every referenced
/// object alive until its ManagementWorker joins. Login callbacks must use the native startup
/// adapter; both are required. No callback below belongs on the AppKit main thread.
struct MacosManagementServices
{
  GatewayConfig& config;
  ConfigStore& config_store;
  ICredentialStore& credentials;
  AuditStore& audit;
  teleprompter::IRepository* scripts;
  MacosGatewayComposition& gateway;
  EnduranceEventLog& endurance_log;
  MacosShutdownSignal& shutdown;
  ManagementStatusSnapshotBuilder& status_snapshot_builder;
  std::function<bool()> sign_in_startup_enabled;
  std::function<void(bool)> set_sign_in_startup;
  std::string build_version;
  std::function<std::string()> obs_directory;
};

ManagementUiCallbacks make_management_callbacks(const MacosManagementServices& services);

/// Native config/monitor mechanisms for the same Windows approval callbacks. Presentation and
/// decision dispatch are supplied by the frontend; this factory does not own or schedule either.
ClientApprovalCallbacks make_macos_client_approval_callbacks(
    GatewayConfig& config,
    ConfigStore& store,
    AuditStore& audit,
    MacosGatewayComposition& gateway,
    std::function<bool(RoleChangeRequest)> present_role,
    std::function<bool(GlobalSettingChangeRequest)> present_global_setting,
    std::function<void(std::string_view)> cancel_device = {});
} // namespace studio_remote::macos
