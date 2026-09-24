#pragma once

#include <studio_remote/ui/management_approval_dispatcher.hpp>
#include <studio_remote/ui/management_controller.hpp>

#include <functional>
#include <memory>
#include <string>

namespace studio_remote::linux
{
class ShutdownSignal;

/// Activates the already running desktop instance without starting another Companion runtime.
bool activate_existing_management_ui();

class LinuxManagementUi
{
public:
  LinuxManagementUi(std::string companion_name,
                    std::string version,
                    bool obs_connection_configured,
                    bool network_recovery_required,
                    bool show_on_start,
                    ManagementInterfaceMode interface_mode,
                    std::function<void(ManagementInterfaceMode)> interface_mode_changed,
                    ManagementUiCallbacks callbacks,
                    ShutdownSignal& shutdown,
                    std::function<void(const RoleChangeRequest&, bool)> decide_role,
                    std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global);
  ~LinuxManagementUi();
  int run();
  ManagementApprovalRequesters approval_requesters() const;
  /// Safe from runtime threads and after UI destruction; clearing is delivered on the GTK thread.
  std::function<void()> pairing_closed_handler() const;

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};
} // namespace studio_remote::linux
