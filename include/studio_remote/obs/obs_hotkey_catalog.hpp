#pragma once

#include <studio_remote/input/hotkey.hpp>
#include <studio_remote/obs/obs_command_key_support.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace studio_remote
{

struct ObservedObsHotkeyBinding
{
  std::string identifier;
  std::string label;
  std::string key_identifier;
  std::uint32_t modifiers = 0;
};

/// Maps observed assignments to the unchanged public key vocabulary. Unsupported chords retain
/// their identity and label with an empty key list. No platform input injection occurs here.
std::vector<ProductionHotkeyDefinition>
map_obs_hotkey_catalog(const std::vector<ObservedObsHotkeyBinding>& bindings,
                       ObsCommandKeySupport command_key = ObsCommandKeySupport::unavailable);

} // namespace studio_remote
