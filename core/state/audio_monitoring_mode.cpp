#include <studio_remote/state/audio_monitoring_mode.hpp>

namespace studio_remote
{

std::string_view audio_monitoring_mode_name(AudioMonitoringMode mode) noexcept
{
  switch (mode)
  {
  case AudioMonitoringMode::none:
    return "none";
  case AudioMonitoringMode::monitor_only:
    return "monitor-only";
  case AudioMonitoringMode::monitor_and_output:
    return "monitor-and-output";
  }
  return "none";
}

std::optional<AudioMonitoringMode> parse_audio_monitoring_mode(std::string_view value) noexcept
{
  if (value == "none")
    return AudioMonitoringMode::none;
  if (value == "monitor-only")
    return AudioMonitoringMode::monitor_only;
  if (value == "monitor-and-output")
    return AudioMonitoringMode::monitor_and_output;
  return std::nullopt;
}

std::string_view obs_audio_monitor_type(AudioMonitoringMode mode) noexcept
{
  switch (mode)
  {
  case AudioMonitoringMode::none:
    return "OBS_MONITORING_TYPE_NONE";
  case AudioMonitoringMode::monitor_only:
    return "OBS_MONITORING_TYPE_MONITOR_ONLY";
  case AudioMonitoringMode::monitor_and_output:
    return "OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT";
  }
  return "OBS_MONITORING_TYPE_NONE";
}

std::optional<AudioMonitoringMode> parse_obs_audio_monitor_type(std::string_view value) noexcept
{
  if (value == "OBS_MONITORING_TYPE_NONE")
    return AudioMonitoringMode::none;
  if (value == "OBS_MONITORING_TYPE_MONITOR_ONLY")
    return AudioMonitoringMode::monitor_only;
  if (value == "OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT")
    return AudioMonitoringMode::monitor_and_output;
  return std::nullopt;
}

} // namespace studio_remote
