#include "recording_pause_policy.hpp"

namespace studio_remote::obs_plugin
{

void RecordingPauseProbeLifecycle::frontend_finished_loading() noexcept
{
  ready_ = true;
}

void RecordingPauseProbeLifecycle::reset() noexcept
{
  ready_ = false;
}

bool RecordingPauseProbeLifecycle::ready() const noexcept
{
  return ready_;
}

bool recording_pause_available(const RecordingPauseConfiguration& configuration) noexcept
{
  bool shares_stream_encoder = false;
  if (configuration.advanced_output_mode)
  {
    shares_stream_encoder = configuration.custom_ffmpeg_recording
                                ? configuration.ffmpeg_output_to_file
                                : configuration.recording_encoder == "none";
  }
  else
  {
    shares_stream_encoder = configuration.simple_recording_quality == "Stream";
  }

  return !shares_stream_encoder && configuration.output_exists &&
         configuration.output_supports_pause;
}

} // namespace studio_remote::obs_plugin
