#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <source_location>
#include <studio_remote/common/types.hpp>
#include <unordered_map>

namespace studio_remote
{
class IObsAdapter;

struct ObsQuickTransitionSelection
{
  std::optional<std::int32_t> native_id;
  std::optional<std::uint32_t> duration_ms;
};

/// Client-owned OBS protocol identity cache. Its mutex protects both maps; no external callbacks
/// or I/O execute while locked. Replacement and resolution follow the Windows request mapping.
class ObsQuickTransitionIdentities final
{
public:
  /// Cache backend identities and remove those private fields from the outgoing catalog.
  void replace(json& catalog);
  /// Translate a string quickTransitionId and remove it before sending backend arguments.
  ObsQuickTransitionSelection resolve(json& data) const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> backend_names_;
  std::unordered_map<std::string, std::int32_t> native_ids_;
};

/// Prepare the Windows WebSocket fallback in order: select name, apply unsigned duration, clear
/// arguments. If either subrequest fails, propagate it before the final trigger can be sent.
void prepare_obs_transition_fallback(IObsAdapter& adapter, json& data, std::source_location caller);
} // namespace studio_remote
