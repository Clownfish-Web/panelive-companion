#include "device_role_protocol.hpp"
#include "http_server.hpp"
#include <studio_remote/common/client_request_lifetime.hpp>

#include <exception>
#include <utility>

namespace studio_remote
{
GatewayHttpServer::GenericCommandOutcome
GatewayHttpServer::execute_role_request(const Device& device,
                                        std::string_view command_id,
                                        Role requested_role,
                                        std::uint64_t current_sequence)
{
  const auto cache_key = device.id + ":" + std::string(command_id);
  const auto operation_id = std::to_string(monotonic_ns());
  GenericCommandOutcome outcome{
      202, {{"status", "accepted"}, {"commandId", command_id}, {"operationId", operation_id}}};
  {
    std::lock_guard lock(generic_command_cache_mutex_);
    const auto pending = role_request_operations_.find(cache_key);
    if (pending != role_request_operations_.end() && !pending->second.command_operation_id)
      return {503,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "permission-required"},
               {"currentStateSequence", current_sequence}}};
    const auto [found, inserted] =
        generic_command_cache_.try_emplace(cache_key, std::pair{Clock::now(), outcome});
    if (!inserted)
      return found->second.second;
    // Completion may run inside the callback, or on another thread before it returns.
    role_request_operations_.emplace(cache_key, PendingRoleRequest{operation_id, requested_role});
  }
  RoleChangeRequestHandler handler;
  {
    std::lock_guard lock(role_change_request_handler_mutex_);
    handler = role_change_request_handler_;
  }
  auto disposition = RoleChangeRequestDisposition::unavailable;
  try
  {
    disposition = requested_role == device.role ? RoleChangeRequestDisposition::approved
                  : handler                     ? handler(device, command_id, requested_role)
                                                : RoleChangeRequestDisposition::unavailable;
  }
  catch (const std::exception&)
  {
    provider_session(device).runtime->audit().command(
        device.id, "request-access", device.id, "approval_handler_failed", operation_id);
  }
  if (disposition == RoleChangeRequestDisposition::unavailable)
  {
    GenericCommandOutcome rejected{503,
                                   {{"status", "rejected"},
                                    {"commandId", command_id},
                                    {"reason", "permission-required"},
                                    {"currentStateSequence", current_sequence}}};
    std::lock_guard lock(generic_command_cache_mutex_);
    generic_command_cache_.insert_or_assign(cache_key, std::pair{Clock::now(), rejected});
    role_request_operations_.erase(cache_key);
    return rejected;
  }
  if (disposition == RoleChangeRequestDisposition::approved)
    complete_role_change_request(device.id, command_id, requested_role, true);
  return outcome;
}

GatewayHttpServer::GenericCommandOutcome GatewayHttpServer::execute_role_extension(
    const Device& device, std::string_view request_id, Role requested_role)
{
  constexpr std::size_t maximum_pending_role_requests = 32;
  if (requested_role == device.role)
    return {200,
            {{"type", "deviceRoleChangeRequest"},
             {"requestId", request_id},
             {"status", "unchanged"},
             {"role", role_name(requested_role)}}};

  const GenericCommandOutcome unavailable{503, {{"error", "local_approval_unavailable"}}};
  const auto pending_response = [&](Role role) -> GenericCommandOutcome
  {
    return {202,
            {{"type", "deviceRoleChangeRequest"},
             {"requestId", request_id},
             {"status", "pending"},
             {"requestedRole", role_name(role)}}};
  };
  const auto key = device.id + ":" + std::string(request_id);
  expire_client_requests(Clock::now());
  {
    std::lock_guard lock(generic_command_cache_mutex_);
    if (const auto found = role_request_operations_.find(key);
        found != role_request_operations_.end())
      return found->second.command_operation_id ? unavailable
                                                : pending_response(found->second.requested_role);
    if (role_request_operations_.size() >= maximum_pending_role_requests)
      return unavailable;
    role_request_operations_.emplace(key, PendingRoleRequest{std::nullopt, requested_role});
  }
  RoleChangeRequestHandler handler;
  {
    std::lock_guard lock(role_change_request_handler_mutex_);
    handler = role_change_request_handler_;
  }
  auto disposition = RoleChangeRequestDisposition::unavailable;
  try
  {
    if (handler)
      disposition = handler(device, request_id, requested_role);
  }
  catch (const std::exception&)
  {
    provider_session(device).runtime->audit().command(
        device.id, "request-device-role", device.id, "approval_handler_failed", request_id);
  }
  if (disposition == RoleChangeRequestDisposition::unavailable)
  {
    std::lock_guard lock(generic_command_cache_mutex_);
    role_request_operations_.erase(key);
    return unavailable;
  }
  if (disposition == RoleChangeRequestDisposition::pending)
    return pending_response(requested_role);
  complete_role_change_request(device.id, request_id, requested_role, true);
  return {200,
          {{"type", "deviceRoleChangeRequest"},
           {"requestId", request_id},
           {"status", "approved"},
           {"requestedRole", role_name(requested_role)}}};
}

void GatewayHttpServer::complete_role_change_request(std::string_view device_id,
                                                     std::string_view request_id,
                                                     Role requested_role,
                                                     bool approved)
{
  static_cast<void>(requested_role);
  std::optional<PendingRoleRequest> pending;
  {
    std::lock_guard lock(generic_command_cache_mutex_);
    if (const auto found =
            role_request_operations_.find(std::string(device_id) + ":" + std::string(request_id));
        found != role_request_operations_.end())
    {
      if (Clock::now() - found->second.admitted_at < maximum_client_request_age)
        pending = std::move(found->second);
      role_request_operations_.erase(found);
    }
  }
  if (!pending)
    return;
  if (!pending->command_operation_id)
  {
    queue_message(
        device_role_change_request_result(request_id, pending->requested_role, approved).dump(),
        std::string(device_id),
        std::nullopt,
        ClientDeliveryPriority::control);
    return;
  }
  const auto& operation_id = *pending->command_operation_id;
  std::uint64_t state_sequence = 0;
  for (const auto& device : connected_devices())
    if (device.id == device_id)
      state_sequence = provider_session(device).runtime->state().revision();
  const auto message = approved ? json{{"type", "commandCompleted"},
                                       {"commandId", request_id},
                                       {"operationId", operation_id},
                                       {"resultingStateSequence", state_sequence}}
                                : json{{"type", "commandRejected"},
                                       {"commandId", request_id},
                                       {"operationId", operation_id},
                                       {"reason", "permission-required"},
                                       {"currentStateSequence", state_sequence}};
  queue_message(
      message.dump(), std::string(device_id), std::nullopt, ClientDeliveryPriority::control);
}

} // namespace studio_remote
