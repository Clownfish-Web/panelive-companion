#pragma once

#include <string_view>

namespace studio_remote
{

struct RecordingState
{
  bool active = false;
  bool paused = false;
  std::string_view state = "stopped";
};

inline RecordingState normalize_recording_state(bool output_active, bool output_paused) noexcept
{
  if (output_paused)
    return {true, true, "paused"};
  if (output_active)
    return {true, false, "recording"};
  return {};
}

} // namespace studio_remote
