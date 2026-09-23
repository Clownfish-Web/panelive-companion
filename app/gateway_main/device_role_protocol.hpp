#pragma once

#include <studio_remote/credentials/audit_store.hpp>

#include <cstdint>

namespace studio_remote
{
inline bool role_change_requires_local_approval(Role requested_role)
{
  return requested_role == Role::operator_;
}

inline json device_role_changed_message(const DeviceRoleChange& change)
{
  return {{"type", "deviceRoleChanged"},
          {"deviceId", change.device_id},
          {"productionProvider", change.production_provider},
          {"previousRole", role_name(change.previous_role)},
          {"role", role_name(change.role)}};
}

inline json
device_role_change_request_result(std::string_view request_id, Role requested_role, bool approved)
{
  return {{"type", "deviceRoleChangeRequestResult"},
          {"requestId", request_id},
          {"status", approved ? "approved" : "denied"},
          {"requestedRole", role_name(requested_role)}};
}

inline std::string forgotten_device_close_payload()
{
  constexpr std::uint16_t close_code = 4001;
  std::string payload;
  payload.push_back(static_cast<char>((close_code >> 8U) & 0xffU));
  payload.push_back(static_cast<char>(close_code & 0xffU));
  payload += "device forgotten";
  return payload;
}
} // namespace studio_remote
