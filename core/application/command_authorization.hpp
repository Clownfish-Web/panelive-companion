#pragma once

#include <studio_remote/credentials/audit_store.hpp>

namespace studio_remote
{
// An admitted control intent belongs to one pairing and role revision. Restoring an earlier
// role must not revive work admitted before its withdrawal. No lock spans the caller's dispatch.
inline bool has_current_control_authority(AuditStore& audit, const Device& device)
{
  if (device.role < Role::operator_)
    return false;
  const auto current = audit.active_device(device.id);
  return current && current->role >= Role::operator_ &&
         current->production_provider == device.production_provider &&
         current->pairing_generation == device.pairing_generation &&
         current->role_revision == device.role_revision;
}
} // namespace studio_remote
