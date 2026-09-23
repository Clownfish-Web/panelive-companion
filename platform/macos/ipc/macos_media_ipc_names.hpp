#pragma once

#include "macos_ipc_names.hpp"

#include <string>

namespace studio_remote::macos
{
struct MediaIpcNames
{
  std::string video = ipc_names::video_frames;
  std::string video_control = ipc_names::video_capture_control;
  std::string audio = ipc_names::audio_monitor;
  std::string audio_control = ipc_names::audio_monitor_control;
};
} // namespace studio_remote::macos
