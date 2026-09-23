#pragma once

#include <studio_remote/input/hotkey.hpp>
#include <studio_remote/obs/obs_command_key_support.hpp>

#include <span>
#include <string>

namespace studio_remote
{
struct ObsHotkeySequence
{
  std::string key_id;
  bool shift = false;
  bool control = false;
  bool alt = false;
  bool command = false;
};

/// Validates the public chord against OBS's supported key-sequence model before any request.
/// Throws HotkeyExecutionError with the Windows adapter's stable reason and message on failure.
ObsHotkeySequence
make_obs_hotkey_sequence(std::span<const HotkeyKey> keys,
                         ObsCommandKeySupport command_key = ObsCommandKeySupport::unavailable);
} // namespace studio_remote
