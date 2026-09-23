#include "client_approval_callbacks.hpp"

#include "device_role_protocol.hpp"

#include <studio_remote/provider/provider_catalog.hpp>

#include <stdexcept>
#include <utility>

namespace studio_remote
{
namespace
{
using Disposition = GatewayHttpServer::RoleChangeRequestDisposition;

Disposition admitted_request(AuditStore& audit,
                             const ClientApprovalActions& actions,
                             std::string_view device_id)
{
  try
  {
    if (audit.active_device_role(device_id))
      return Disposition::pending;
  }
  catch (...)
  {
    if (actions.cancel_device)
      actions.cancel_device(device_id);
    throw;
  }
  // Removal may have scanned the dispatcher before this HTTP request registered its prompt.
  if (actions.cancel_device)
    actions.cancel_device(device_id);
  return Disposition::unavailable;
}

void decide_role(AuditStore& audit,
                 const ClientApprovalActions& actions,
                 const RoleChangeRequest& request,
                 bool approved)
{
  if (approved)
  {
    try
    {
      approved = audit.active_device_role(request.device_id).has_value();
      if (approved)
      {
        audit.set_role(request.device_id,
                       request.requested_role,
                       actions.single_operator_mode()
                           ? OperatorAssignmentPolicy::transfer_existing_operators
                           : OperatorAssignmentPolicy::allow_multiple_operators);
        approved = audit.active_device_role(request.device_id).has_value();
      }
      audit.command(request.device_id,
                    "request-device-role",
                    request.device_id,
                    approved ? "approved" : "denied",
                    "");
    }
    catch (...)
    {
      actions.complete_role(request.device_id, request.request_id, request.requested_role, false);
      throw;
    }
  }
  else
  {
    audit.command(request.device_id, "request-device-role", request.device_id, "denied", "");
  }
  actions.complete_role(request.device_id, request.request_id, request.requested_role, approved);
}

void decide_global_setting(AuditStore& audit,
                           const ClientApprovalActions& actions,
                           const GlobalSettingChangeRequest& request,
                           bool approved)
{
  if (approved)
  {
    try
    {
      approved = audit.active_device_role(request.device_id) == Role::admin;
      if (approved)
      {
        if (request.setting_id != "audio-source-audition-bus" ||
            !request.requested_value.is_boolean())
          throw std::invalid_argument("Unsupported global setting request");
        actions.apply_audition_bus(request.requested_value.get<bool>());
      }
      audit.command(request.device_id,
                    "set-audio-source-audition-bus",
                    request.setting_id,
                    approved ? "approved" : "denied",
                    request.request_id);
    }
    catch (...)
    {
      actions.complete_global_setting(request.device_id, request.request_id, false);
      throw;
    }
  }
  else
  {
    audit.command(request.device_id,
                  "set-audio-source-audition-bus",
                  request.setting_id,
                  "denied",
                  request.request_id);
  }
  actions.complete_global_setting(request.device_id, request.request_id, approved);
}

Disposition request_role(AuditStore& audit,
                         const ClientApprovalActions& actions,
                         const Device& device,
                         std::string_view request_id,
                         Role requested_role)
{
  if (!audit.active_device_role(device.id))
    return Disposition::unavailable;
  if (!role_change_requires_local_approval(requested_role))
  {
    bool approved = false;
    try
    {
      audit.set_role(device.id, Role::viewer);
      approved = audit.active_device_role(device.id).has_value();
      audit.command(
          device.id, "request-device-role", device.id, approved ? "approved" : "denied", "");
    }
    catch (...)
    {
      actions.complete_role(device.id, request_id, Role::viewer, false);
      throw;
    }
    actions.complete_role(device.id, request_id, Role::viewer, approved);
    return approved ? Disposition::approved : Disposition::unavailable;
  }
  const auto* provider = find_known_production_provider(device.production_provider);
  if (actions.present_role && actions.present_role({std::string(request_id),
                                                    device.id,
                                                    device.name,
                                                    requested_role,
                                                    device.production_provider,
                                                    provider ? std::string(provider->display_name)
                                                             : device.production_provider}))
    return admitted_request(audit, actions, device.id);
  return Disposition::unavailable;
}

Disposition request_global_setting(AuditStore& audit,
                                   const ClientApprovalActions& actions,
                                   const Device& device,
                                   std::string_view request_id,
                                   std::string_view setting_id,
                                   const json& value)
{
  if (audit.active_device_role(device.id) != Role::admin)
    return Disposition::unavailable;
  constexpr std::string_view setting_name = "Activate audio source audition bus";
  constexpr std::string_view warning =
      "Enabling this intentionally extends OBS behavior. Monitored Preview-only "
      "sources are kept active and may be audible through the configured OBS monitoring "
      "device. They are not inserted into the Program scene or actual output mix.";
  if (actions.present_global_setting && actions.present_global_setting({std::string(request_id),
                                                                        device.id,
                                                                        device.name,
                                                                        std::string(setting_id),
                                                                        std::string(setting_name),
                                                                        std::string(warning),
                                                                        value}))
    return admitted_request(audit, actions, device.id);
  return Disposition::unavailable;
}
} // namespace

ClientApprovalCallbacks make_client_approval_callbacks(AuditStore& audit,
                                                       ClientApprovalActions actions)
{
  return {
      [&audit, actions](const RoleChangeRequest& request, bool approved)
      { decide_role(audit, actions, request, approved); },
      [&audit, actions](const GlobalSettingChangeRequest& request, bool approved)
      { decide_global_setting(audit, actions, request, approved); },
      [&audit, actions](const Device& device, std::string_view request_id, Role requested_role)
      { return request_role(audit, actions, device, request_id, requested_role); },
      [&audit, actions](const Device& device,
                        std::string_view request_id,
                        std::string_view setting_id,
                        const json& value)
      { return request_global_setting(audit, actions, device, request_id, setting_id, value); }};
}
} // namespace studio_remote
