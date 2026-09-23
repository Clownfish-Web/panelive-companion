#pragma once

#include "http_server.hpp"

#include <studio_remote/ui/client_approval_request.hpp>

#include <functional>
#include <string_view>

namespace studio_remote
{
/// Native composition mechanisms used by the unchanged Windows request/decision sequence.
/// Every callback borrows root resources. Those resources must outlive UI decisions and HTTP drain.
struct ClientApprovalActions
{
  std::function<bool()> single_operator_mode;
  /// Copy config, change the value, save, assign, apply monitor mix, then update canonical state.
  std::function<void(bool)> apply_audition_bus;
  std::function<void(std::string_view, std::string_view, Role, bool)> complete_role;
  std::function<void(std::string_view, std::string_view, bool)> complete_global_setting;
  std::function<bool(RoleChangeRequest)> present_role;
  std::function<bool(GlobalSettingChangeRequest)> present_global_setting;
  std::function<void(std::string_view)> cancel_device = {};
};

struct ClientApprovalCallbacks
{
  std::function<void(const RoleChangeRequest&, bool)> decide_role;
  std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global_setting;
  GatewayHttpServer::RoleChangeRequestHandler request_role;
  GatewayHttpServer::GlobalSettingChangeRequestHandler request_global_setting;
};

/// Extracted Windows composition callbacks. No additional owner, dispatcher or timeout is created.
/// HTTP invokes requests; the frontend invokes decisions on its configuration execution context.
ClientApprovalCallbacks make_client_approval_callbacks(AuditStore& audit,
                                                       ClientApprovalActions actions);
} // namespace studio_remote
