#pragma once

#include "../../shared/capture/video_capture_worker.hpp"
#include "../ipc/macos_ipc_names.hpp"

#include <string>

namespace studio_remote::macos
{
/// One external owner serializes lifecycle. Callbacks must return promptly, cannot stop/destroy
/// this worker, and their targets must outlive it. Health can be read from any thread or callback.
class MacosCaptureWorker final
{
public:
  using TrackHandler = VideoCaptureWorker::TrackHandler;
  using HealthHandler = VideoCaptureWorker::HealthHandler;
  using DemandProvider = VideoCaptureWorker::DemandProvider;
  using GenerationHandler = VideoCaptureWorker::GenerationHandler;
  using MonotonicClock = VideoCaptureWorker::MonotonicClock;

  explicit MacosCaptureWorker(TrackHandler track_handler,
                              HealthHandler health_handler = {},
                              DemandProvider demand_provider = {},
                              GenerationHandler generation_handler = {},
                              std::string frame_name = ipc_names::video_frames,
                              std::string control_name = ipc_names::video_capture_control,
                              MonotonicClock clock = {});
  ~MacosCaptureWorker();
  MacosCaptureWorker(const MacosCaptureWorker&) = delete;
  MacosCaptureWorker& operator=(const MacosCaptureWorker&) = delete;

  void start();
  void stop();
  json health() const;

private:
  VideoCaptureWorker worker_;
};
} // namespace studio_remote::macos
