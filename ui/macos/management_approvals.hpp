#pragma once

#include <studio_remote/ui/management_approval_dispatcher.hpp>

@class PLManagementWindow;

namespace studio_remote::macos
{
class ManagementWorker;

/// Construct/close in the native UI owner on AppKit's main thread. Window and worker outlive
/// closure; weak requesters and queued tasks never retain them beyond the dispatcher's lifetime.
std::unique_ptr<ManagementApprovalDispatcher> make_management_approvals(
    PLManagementWindow* window,
    ManagementWorker& worker,
    std::function<void(const RoleChangeRequest&, bool)> decide_role,
    std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global);
} // namespace studio_remote::macos
