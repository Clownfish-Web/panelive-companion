#pragma once

#include <obs.h>

#include <cstdint>
#include <functional>

namespace studio_remote::obs_plugin
{

class ObsPreviewOutput
{
public:
  using FrameHandler = std::function<void(video_data& frame)>;

  explicit ObsPreviewOutput(FrameHandler frame_handler);
  ~ObsPreviewOutput();

  ObsPreviewOutput(const ObsPreviewOutput&) = delete;
  ObsPreviewOutput& operator=(const ObsPreviewOutput&) = delete;

  bool start(video_t* video, const video_scale_info& conversion);
  void stop();

private:
  struct OutputContext;

  static const char* output_name(void* type_data);
  static void* create_output(obs_data_t* settings, obs_output_t* output);
  static void destroy_output(void* data);
  static bool start_output(void* data);
  static void stop_output(void* data, std::uint64_t timestamp);
  static void receive_frame(void* data, video_data* frame);
  static void register_output_type();

  FrameHandler frame_handler_;
  obs_output_t* output_ = nullptr;
};

} // namespace studio_remote::obs_plugin
