#include "linux_client_approvals.hpp"

#include "linux_management_composition.hpp"

#include <utility>

namespace studio_remote::linux
{
ClientApprovalCallbacks linux_client_approval_callbacks(const LinuxCompanionServices& services,
                                                        ManagementApprovalRequesters requesters)
{
  return make_client_approval_callbacks(
      services.audit,
      {[services] { return services.config.enforce_single_operator_client_mode; },
       [services](bool enabled)
       {
         auto updated = services.config;
         updated.audio_source_audition_bus = enabled;
         services.config_store.save(updated);
         services.config = std::move(updated);
         apply_audio_mix_configuration(services.config, services.audio_monitor, services.runtime);
       },
       [services](std::string_view device_id, std::string_view request_id, Role role, bool approved)
       { services.http.complete_role_change_request(device_id, request_id, role, approved); },
       [services](std::string_view device_id, std::string_view request_id, bool approved)
       { services.http.complete_global_setting_change_request(device_id, request_id, approved); },
       std::move(requesters.role),
       std::move(requesters.global_setting),
       std::move(requesters.cancel_device)});
}
} // namespace studio_remote::linux
