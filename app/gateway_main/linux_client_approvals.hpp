#pragma once

#include "client_approval_callbacks.hpp"
#include "linux_companion_services.hpp"
#include <studio_remote/ui/management_approval_dispatcher.hpp>

namespace studio_remote::linux
{
/// Routes mobile role-change and global-setting requests through the management window's approval
/// prompts and completes each request exactly once on the HTTP server. The composition root clears
/// the HTTP handlers before the management UI is destroyed.
ClientApprovalCallbacks
linux_client_approval_callbacks(const LinuxCompanionServices& services,
                                ManagementApprovalRequesters requesters = {});
} // namespace studio_remote::linux
