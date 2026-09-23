#pragma once

#include <cstdint>
#include <string>
#include <studio_remote/common/types.hpp>
#include <vector>

namespace studio_remote
{
struct ObservedQuickTransition
{
  std::int32_t transition_id = 0;
  std::uint32_t duration_ms = 0;
  std::string label;
};

/// OBS protocol-boundary mapping of live helper buttons, preserving Windows ordering, matching,
/// duration precedence and configured Fade-to-Black semantics. No platform resources are accessed.
json map_obs_quick_transition_hotkey_catalog(
    const std::vector<ObservedQuickTransition>& quick_transitions,
    const json& obs_transition_catalog,
    const json& configured_quick_transitions = json::array());

/// Map saved quick-transition buttons; backend transition types alone never become buttons.
json map_obs_quick_transition_catalog(const json& scene_collection,
                                      const json& obs_transition_catalog);
} // namespace studio_remote
