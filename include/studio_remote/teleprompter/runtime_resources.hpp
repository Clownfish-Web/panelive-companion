#pragma once

#include <studio_remote/teleprompter/session.hpp>

#include <functional>
#include <map>
#include <memory>

namespace studio_remote::teleprompter
{
/// Current paired identity and allowed session roles, supplied without persistent I/O.
struct SessionDevice
{
  std::string id;
  std::string provider;
  std::vector<SessionRole> allowed_roles;
  std::uint64_t pairing_generation = 0;
  std::uint64_t session_authorization_generation = 0;
};
using SessionDevices = std::map<std::string, SessionDevice, std::less<>>;
using SessionDeviceSource = std::function<SessionDevices()>;

struct ScriptAttachment
{
  std::uint64_t pairing_generation = 0;
  std::string grant_id;
  bool operator==(const ScriptAttachment&) const = default;
};

/// Immutable compiled library revision shared by active sessions. No per-report recompilation.
struct LoadedScript
{
  // Local assignments bind a script to the current incarnation of each paired device.
  std::map<std::string, ScriptAttachment, std::less<>> assignments;
  Script script;
  std::vector<std::uint64_t> word_offsets;

  bool assigned_to(const SessionDevice& device) const noexcept
  {
    const auto found = assignments.find(device.id);
    return found != assignments.end() &&
           found->second.pairing_generation == device.pairing_generation;
  }
};
using ScriptCatalogSnapshot =
    std::map<std::string, std::shared_ptr<const LoadedScript>, std::less<>>;
using ScriptCatalogView = std::shared_ptr<const ScriptCatalogSnapshot>;
using ScriptCatalogSource = std::function<ScriptCatalogView()>;
} // namespace studio_remote::teleprompter
