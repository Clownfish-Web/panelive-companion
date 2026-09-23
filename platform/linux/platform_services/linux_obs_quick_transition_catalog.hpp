#pragma once

#include "linux_obs_quick_transition_control.hpp"

#include <filesystem>
#include <studio_remote/common/types.hpp>
#include <vector>

namespace studio_remote::linux
{

json map_obs_quick_transition_hotkey_catalog(
    const std::vector<ObservedQuickTransition>& quick_transitions,
    const json& obs_transition_catalog,
    const json& configured_quick_transitions = json::array());
json map_obs_quick_transition_catalog(const json& scene_collection,
                                      const json& obs_transition_catalog);
json load_obs_quick_transition_catalog(const std::filesystem::path& obs_configuration_root,
                                       const json& obs_transition_catalog);
std::filesystem::path obs_configuration_root();

} // namespace studio_remote::linux
