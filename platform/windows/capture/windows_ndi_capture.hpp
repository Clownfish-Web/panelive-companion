#pragma once

#include "windows_ndi_runtime.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <studio_remote/common/types.hpp>
#include <studio_remote/media/media.hpp>
#include <studio_remote/vmix/vmix_video_stream_adapter.hpp>
#include <thread>

namespace studio_remote::windows
{

class NdiCaptureManager final : public vmix::IVmixVideoStreamAdapter
{
public:
  using CanvasAspectRatioProvider = std::function<double()>;

  NdiCaptureManager(std::shared_ptr<NdiRuntime> runtime,
                    NdiSourceDescriptor program_source,
                    NdiSourceDescriptor preview_source,
                    vmix::VideoStreamAdapterCallbacks video_callbacks,
                    CanvasAspectRatioProvider canvas_aspect_ratio_provider = {});
  ~NdiCaptureManager();

  NdiCaptureManager(const NdiCaptureManager&) = delete;
  NdiCaptureManager& operator=(const NdiCaptureManager&) = delete;

  void start() override;
  void stop() override;
  vmix::VideoStreamAvailability availability() const override;
  json health() const;

private:
  struct TrackState
  {
    std::string name;
    NdiSourceDescriptor source;
    std::thread worker;
    std::uint64_t received_frames = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t last_frame_ns = 0;
    std::uint32_t source_width = 0;
    std::uint32_t source_height = 0;
    std::uint32_t delivered_width = 0;
    std::uint32_t delivered_height = 0;
    float picture_aspect_ratio = 0.0F;
    double canvas_aspect_ratio = 0.0;
    std::string error;
  };

  void receive_loop(TrackState& track);
  void publish_health(const TrackState& track, bool available) const;

  std::shared_ptr<NdiRuntime> runtime_;
  vmix::VideoStreamAdapterCallbacks video_callbacks_;
  CanvasAspectRatioProvider canvas_aspect_ratio_provider_;
  mutable std::mutex mutex_;
  TrackState program_{"program"};
  TrackState preview_{"preview"};
  std::atomic<bool> stopping_{false};
  bool initialized_ = false;
};

} // namespace studio_remote::windows
