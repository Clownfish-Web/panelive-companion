#include "session_device_source.hpp"

#include <studio_remote/credentials/audit_store.hpp>

#include <vector>

namespace studio_remote::teleprompter
{
namespace
{
std::vector<SessionRole> allowed_roles(const Device& device)
{
  if (device.production_provider == "companion-teleprompter")
  {
    if (device.role == Role::operator_)
      return {SessionRole::controller};
    if (device.role == Role::viewer)
      return {SessionRole::master, SessionRole::monitor};
    return {};
  }
  if (device.production_provider == "obs" || device.production_provider == "vmix" ||
      device.production_provider == "streamlabs")
    return {SessionRole::master, SessionRole::controller, SessionRole::monitor};
  return {};
}
} // namespace

SessionDeviceSource session_device_source(AuditStore& audit)
{
  return [&audit]
  {
    SessionDevices result;
    for (const auto& device : audit.devices())
      result.emplace(device.id,
                     SessionDevice{device.id,
                                   device.production_provider,
                                   allowed_roles(device),
                                   device.pairing_generation,
                                   device.session_authorization_generation});
    return result;
  };
}
} // namespace studio_remote::teleprompter
