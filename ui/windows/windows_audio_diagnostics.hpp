#pragma once

#include "../../platform/windows/capture/windows_audio_diagnostic_player.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{
struct AudioDiagnosticsCallbacks
{
  std::function<std::vector<AudioOutputDevice>()> devices;
  std::function<AudioDiagnosticPlaybackStatus()> status;
  std::function<void(std::string_view, AudioDiagnosticTap)> start;
  std::function<void()> stop;
  std::function<void(float)> set_gain_db;
  std::function<void()> begin_debug_log;
  std::function<void()> complete_debug_log;
  std::function<bool()> debug_log_active;
  std::function<bool()> debug_log_available;
  std::function<std::string()> debug_log_report;
};
} // namespace studio_remote::windows
