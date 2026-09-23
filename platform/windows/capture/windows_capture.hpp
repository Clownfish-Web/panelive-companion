#pragma once

#include "../../shared/capture/video_capture_worker.hpp"

namespace studio_remote::windows
{
/// One external owner serializes lifecycle. Callbacks must return promptly, cannot stop/destroy
/// this worker, and their targets must outlive it. Health can be read from any thread or callback.
class CaptureManager final
{
public:
  using TrackHandler = VideoCaptureWorker::TrackHandler;
  using HealthHandler = VideoCaptureWorker::HealthHandler;
  using DemandProvider = VideoCaptureWorker::DemandProvider;
  using GenerationHandler = VideoCaptureWorker::GenerationHandler;

  explicit CaptureManager(TrackHandler track_handler,
                          HealthHandler health_handler = {},
                          DemandProvider demand_provider = {},
                          GenerationHandler generation_handler = {});
  ~CaptureManager();
  CaptureManager(const CaptureManager&) = delete;
  CaptureManager& operator=(const CaptureManager&) = delete;

  void start();
  void stop();
  json health() const;

private:
  VideoCaptureWorker worker_;
};
} // namespace studio_remote::windows
