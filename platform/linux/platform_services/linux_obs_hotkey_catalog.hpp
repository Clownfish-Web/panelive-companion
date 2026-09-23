#pragma once

#include "linux_obs_quick_transition_control.hpp"

#include <studio_remote/input/hotkey.hpp>

#include <vector>

namespace studio_remote::linux
{

std::vector<ProductionHotkeyDefinition>
map_obs_hotkey_catalog(const std::vector<ObservedObsHotkeyBinding>& bindings);

} // namespace studio_remote::linux
