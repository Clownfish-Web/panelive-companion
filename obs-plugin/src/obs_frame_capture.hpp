#pragma once

#include "capture_lifecycle.hpp"
#include "obs_preview_output.hpp"
#include "shared_frame_publisher.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <studio_remote/media/frame_cadence.hpp>

#include <atomic>
#include <cstdint>
#include <functional>

namespace studio_remote::obs_plugin
{

class ObsFrameCapture
{
public:
  explicit ObsFrameCapture(SharedFramePublisher& publisher);
  ~ObsFrameCapture();

  ObsFrameCapture(const ObsFrameCapture&) = delete;
  ObsFrameCapture& operator=(const ObsFrameCapture&) = delete;

  bool start();
  void stop();
  void poll_demand(std::uint64_t now_ns);
  void on_status(std::function<void(CompanionStatusSnapshot)> handler);

private:
  static void receive_program_frame(void* context, video_data* frame);
  static void handle_frontend_event(obs_frontend_event event, void* context);

  void on_program_frame(video_data& frame);
  void on_preview_frame(video_data& frame);
  void refresh_preview_source();
  bool activate_capture();
  void deactivate_capture();
  void publish_status();
  bool create_preview_view();
  void destroy_preview_view();

  SharedFramePublisher& publisher_;
  CaptureLifecycle lifecycle_;
  ipc::VideoCaptureDemandSnapshot demand_;
  std::function<void(CompanionStatusSnapshot)> status_handler_;
  video_scale_info program_conversion_{};
  video_scale_info preview_conversion_{};
  ObsPreviewOutput preview_output_;
  obs_view_t* preview_view_ = nullptr;
  video_t* preview_video_ = nullptr;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  FrameCadence program_cadence_;
  FrameCadence preview_cadence_;
  bool preview_callback_logged_ = false;
  bool reload_completion_pending_ = false;
  bool started_ = false;
  bool frontend_callback_registered_ = false;
  bool program_callback_registered_ = false;
  std::atomic<bool> capture_active_{false};
  std::atomic<std::uint32_t> requested_program_fps_{0};
  std::atomic<std::uint32_t> requested_preview_fps_{0};
  std::uint64_t status_generation_ = 0;
};

} // namespace studio_remote::obs_plugin
