#pragma once

#include <studio_remote/ui/management_approval_dispatcher.hpp>
#include <studio_remote/ui/management_view.hpp>

#include <functional>
#include <memory>
#include <string>

namespace studio_remote::macos
{
class ManagementWorker;

/// Native counterpart of WindowsManagementUi. AppKit presentation stays on the main thread;
/// the shared controller lives on the borrowed worker, which outlives this presentation owner.
/// The root detaches process polling and approval request hooks before destroying this object.
class MacosManagementUi final
{
public:
  MacosManagementUi(ManagementWorker& worker,
                    const std::string& build_version,
                    std::function<void(const RoleChangeRequest&, bool)> decide_role,
                    std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global);
  ~MacosManagementUi();
  MacosManagementUi(const MacosManagementUi&) = delete;
  MacosManagementUi& operator=(const MacosManagementUi&) = delete;

  void show_management();
  void show_pairing(const ManagementPairing& pairing);
  void hide_pairing();
  void shutdown();
  ManagementApprovalRequesters approval_requesters() const;
  /// May be invoked by the exchange thread. Queued main-thread work holds only a weak window.
  std::function<void()> pairing_closed_handler() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
