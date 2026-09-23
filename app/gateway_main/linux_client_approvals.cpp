#include "linux_client_approvals.hpp"

#include "device_role_protocol.hpp"
#include "linux_management_composition.hpp"

#include "../../ui/linux/linux_management_ui.hpp"

#include <stdexcept>

namespace studio_remote::linux
{
namespace
{
using Disposition = GatewayHttpServer::RoleChangeRequestDisposition;

constexpr std::string_view audition_bus_setting_id = "audio-source-audition-bus";
constexpr std::string_view audition_bus_setting_name = "Activate audio source audition bus";
constexpr std::string_view audition_bus_warning =
    "Enabling this intentionally extends OBS behavior. Monitored Preview-only sources are kept "
    "active and may be audible through the configured OBS monitoring device. They are not "
    "inserted into the Program scene or actual output mix.";

void complete_role_change(const LinuxCompanionServices& services,
                          const ManagementRoleChangeRequest& request,
                          Role requested_role,
                          bool approved)
{
  auto& audit = services.audit;
  if (approved)
  {
    try
    {
      audit.set_role(request.device_id,
                     requested_role,
                     services.config.enforce_single_operator_client_mode
                         ? OperatorAssignmentPolicy::transfer_existing_operators
                         : OperatorAssignmentPolicy::allow_multiple_operators);
      audit.command(request.device_id, "request-device-role", request.device_id, "approved", "");
    }
    catch (...)
    {
      services.http.complete_role_change_request(
          request.device_id, request.request_id, requested_role, false);
      throw;
    }
  }
  else
  {
    audit.command(request.device_id, "request-device-role", request.device_id, "denied", "");
  }
  services.http.complete_role_change_request(
      request.device_id, request.request_id, requested_role, approved);
}

Disposition handle_role_change_request(const LinuxCompanionServices& services,
                                       LinuxManagementUi& management_ui,
                                       const Device& device,
                                       std::string_view request_id,
                                       Role requested_role)
{
  if (!role_change_requires_local_approval(requested_role))
  {
    services.audit.set_role(device.id, Role::viewer);
    services.audit.command(device.id, "request-device-role", device.id, "approved", "");
    services.http.complete_role_change_request(device.id, request_id, Role::viewer, true);
    return Disposition::approved;
  }
  const auto request = ManagementRoleChangeRequest{std::string(request_id),
                                                   device.id,
                                                   device.name,
                                                   std::string(role_name(requested_role)),
                                                   "OBS Studio"};
  const bool queued = management_ui.request_role_change(
      request,
      [services, request, requested_role](bool approved)
      { complete_role_change(services, request, requested_role, approved); });
  return queued ? Disposition::pending : Disposition::unavailable;
}

void apply_global_setting_change(const LinuxCompanionServices& services,
                                 const ManagementGlobalSettingChangeRequest& request)
{
  if (request.setting_id != audition_bus_setting_id)
    throw std::invalid_argument("Unsupported global setting request");
  auto& config = services.config;
  config.audio_source_audition_bus = request.requested_value;
  services.config_store.save(config);
  apply_audio_mix_configuration(config, services.audio_monitor, services.runtime);
}

void complete_global_setting_change(const LinuxCompanionServices& services,
                                    const ManagementGlobalSettingChangeRequest& request,
                                    bool approved)
{
  auto& audit = services.audit;
  if (approved)
  {
    try
    {
      apply_global_setting_change(services, request);
      audit.command(request.device_id,
                    "set-audio-source-audition-bus",
                    request.setting_id,
                    "approved",
                    request.request_id);
    }
    catch (...)
    {
      services.http.complete_global_setting_change_request(
          request.device_id, request.request_id, false);
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
  services.http.complete_global_setting_change_request(
      request.device_id, request.request_id, approved);
}

Disposition handle_global_setting_change_request(const LinuxCompanionServices& services,
                                                 LinuxManagementUi& management_ui,
                                                 const Device& device,
                                                 std::string_view request_id,
                                                 std::string_view setting_id,
                                                 const json& value)
{
  const auto request =
      ManagementGlobalSettingChangeRequest{std::string(request_id),
                                           device.id,
                                           device.name,
                                           std::string(setting_id),
                                           std::string(audition_bus_setting_name),
                                           std::string(audition_bus_warning),
                                           value.is_boolean() && value.get<bool>()};
  const bool queued = management_ui.request_global_setting_change(
      request,
      [services, request](bool approved)
      { complete_global_setting_change(services, request, approved); });
  return queued ? Disposition::pending : Disposition::unavailable;
}
} // namespace

void connect_client_approval_requests(const LinuxCompanionServices& services,
                                      LinuxManagementUi& management_ui)
{
  services.http.on_role_change_requested(
      [services,
       &management_ui](const Device& device, std::string_view request_id, Role requested_role)
      {
        return handle_role_change_request(
            services, management_ui, device, request_id, requested_role);
      });
  services.http.on_global_setting_change_requested(
      [services, &management_ui](const Device& device,
                                 std::string_view request_id,
                                 std::string_view setting_id,
                                 const json& value)
      {
        return handle_global_setting_change_request(
            services, management_ui, device, request_id, setting_id, value);
      });
}
} // namespace studio_remote::linux
