#pragma once

#include <optional>
#include <string_view>

namespace studio_remote
{

enum class AudioMonitoringMode
{
  none,
  monitor_only,
  monitor_and_output,
};

std::string_view audio_monitoring_mode_name(AudioMonitoringMode mode) noexcept;
std::optional<AudioMonitoringMode> parse_audio_monitoring_mode(std::string_view value) noexcept;
std::string_view obs_audio_monitor_type(AudioMonitoringMode mode) noexcept;
std::optional<AudioMonitoringMode> parse_obs_audio_monitor_type(std::string_view value) noexcept;

} // namespace studio_remote
