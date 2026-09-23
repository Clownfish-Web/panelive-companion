#pragma once

#include "../capture/windows_audio_diagnostic_player.hpp"

#include <studio_remote/media/audio_monitor_mix_settings.hpp>

#include <cstdint>
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

struct ProductionInstallLocations
{
  std::string configured_vmix_directory;
  std::string detected_vmix_directory;
  std::string vmix_ndi_runtime_directory;
  std::string obs_directory;
};

struct GatewayNetworkStatus
{
  std::string address;
  std::vector<std::string> available_addresses;
  std::uint16_t port = 7443;
  bool listening = false;
  std::string warning;
};

struct GatewayNetworkSettings
{
  std::string address;
  std::uint16_t port = 7443;
};
} // namespace studio_remote::windows
