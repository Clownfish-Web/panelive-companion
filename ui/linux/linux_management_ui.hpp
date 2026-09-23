#pragma once

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
                    ShutdownSignal& shutdown);
  ~LinuxManagementUi();
  int run();
  bool request_role_change(ManagementRoleChangeRequest request,
                           ManagementApprovalDecision decision);
  bool request_global_setting_change(ManagementGlobalSettingChangeRequest request,
                                     ManagementApprovalDecision decision);

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};
} // namespace studio_remote::linux
