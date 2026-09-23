#pragma once

#include "linux_companion_services.hpp"

namespace studio_remote::linux
{
class LinuxManagementUi;

/// Routes mobile role-change and global-setting requests through the management window's approval
/// prompts and completes each request exactly once on the HTTP server. The composition root clears
/// the HTTP handlers before the management UI is destroyed.
void connect_client_approval_requests(const LinuxCompanionServices& services,
                                      LinuxManagementUi& management_ui);
} // namespace studio_remote::linux
