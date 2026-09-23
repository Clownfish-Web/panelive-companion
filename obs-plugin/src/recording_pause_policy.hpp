#pragma once

#include <string_view>

namespace studio_remote::obs_plugin
{

struct RecordingPauseConfiguration
{
  bool advanced_output_mode = false;
  bool custom_ffmpeg_recording = false;
  bool ffmpeg_output_to_file = false;
  std::string_view recording_encoder;
  std::string_view simple_recording_quality;
  bool output_exists = false;
  bool output_supports_pause = false;
};

class RecordingPauseProbeLifecycle
{
public:
  void frontend_finished_loading() noexcept;
  void reset() noexcept;
  bool ready() const noexcept;

private:
  bool ready_ = false;
};

bool recording_pause_available(const RecordingPauseConfiguration& configuration) noexcept;

} // namespace studio_remote::obs_plugin
