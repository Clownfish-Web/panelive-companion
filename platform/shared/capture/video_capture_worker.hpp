#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/ipc/video_capture_control_mapping.hpp>
#include <studio_remote/ipc/video_frame_mapping.hpp>
#include <studio_remote/ipc/video_slot_sequence_tracker.hpp>
#include <studio_remote/media/media.hpp>

#include <condition_variable>
#include <studio_remote/media/video_capture_demand.hpp>
#include <studio_remote/media/video_frame_source.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

namespace studio_remote
{

/// Start/stop have one external lifecycle owner. Callbacks run on the worker and must return
/// promptly; they must not stop or destroy the worker. Health snapshots may be read concurrently.
class VideoCaptureWorker final
{
public:
  using FrameFactory = std::function<std::unique_ptr<IVideoFrameSource>()>;
  using DemandFactory = std::function<std::unique_ptr<IVideoCaptureDemand>(std::uint32_t producer)>;
  using FailureReporter = void (*)(const char* message) noexcept;
  using TrackHandler =
      std::function<void(std::string_view track, std::shared_ptr<const RawVideoFrame> frame)>;
  using HealthHandler = std::function<void(std::string_view track, const json& health)>;
  using DemandProvider = std::function<VideoDemand()>;
  using MonotonicClock = std::function<std::uint64_t()>;
  using GenerationHandler = std::function<void(std::uint32_t generation)>;

  VideoCaptureWorker(FrameFactory frames,
                     DemandFactory demand,
                     MonotonicClock clock,
                     FailureReporter report_failure,
                     TrackHandler track_handler,
                     HealthHandler health_handler,
                     DemandProvider demand_provider,
                     GenerationHandler generation_handler);
  ~VideoCaptureWorker();

  VideoCaptureWorker(const VideoCaptureWorker&) = delete;
  VideoCaptureWorker& operator=(const VideoCaptureWorker&) = delete;

  void start();
  void stop();
  json health() const;

private:
  struct TrackState
  {
    // Sequence identity and activation floor have only the capture thread as owner.
    ipc::VideoSlotSequenceTracker sequence_tracker;
    std::uint64_t received_frames = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t first_frame_ns = 0;
    std::uint64_t last_frame_ns = 0;
    std::uint64_t activation_published_frames = 0;
    double sampled_average_channel = 0.0;
    double sampled_non_black_percent = 0.0;
    bool previously_available = false;
  };

  void capture_loop(std::stop_token stop);
  bool poll_capture();
  bool connect_mapping();
  void disconnect_mapping();
  bool update_video_demand(const VideoDemand& demand);
  bool observe_capture_generation(const VideoProducerSnapshot& producer);
  void read_track(std::string_view name, CapturedVideoTrack track, TrackState& state);
  void publish_health(std::string_view name, TrackState& state);

  const FrameFactory frame_factory_;
  const DemandFactory demand_factory_;
  const MonotonicClock clock_;
  const FailureReporter report_failure_;
  TrackHandler track_handler_;
  HealthHandler health_handler_;
  DemandProvider demand_provider_;
  GenerationHandler generation_handler_;
  mutable std::mutex mutex_;
  TrackState program_;
  TrackState preview_;
  std::uint64_t connected_at_ns_ = 0;
  std::unique_ptr<IVideoFrameSource> frames_;
  std::unique_ptr<IVideoCaptureDemand> control_;
  bool mapping_connected_ = false;
  std::uint32_t producer_pid_ = 0;
  std::uint64_t producer_heartbeat_ns_ = 0;
  VideoDemand current_demand_;
  std::uint64_t demand_generation_ = 0;
  std::optional<std::uint32_t> capture_generation_;
  std::atomic<std::uint64_t> capture_failures_{0};
  std::mutex wait_mutex_;
  std::condition_variable_any wake_;
  std::mutex lifecycle_mutex_;
  std::thread::id capture_thread_id_;
  std::jthread capture_thread_;
};

} // namespace studio_remote
