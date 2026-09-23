#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/ipc/video_capture_control_mapping.hpp>
#include <studio_remote/ipc/video_frame_mapping.hpp>
#include <studio_remote/ipc/video_slot_sequence_tracker.hpp>
#include <studio_remote/media/media.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace studio_remote::linux
{
class CaptureManager
{
public:
  using TrackHandler = std::function<void(std::string_view, std::shared_ptr<const RawVideoFrame>)>;
  using HealthHandler = std::function<void(std::string_view, const json&)>;
  using DemandProvider = std::function<VideoDemand()>;
  using GenerationHandler = std::function<void(std::uint32_t)>;

  CaptureManager(TrackHandler track_handler,
                 HealthHandler health_handler,
                 DemandProvider demand_provider,
                 GenerationHandler generation_handler);
  ~CaptureManager();
  void start();
  void stop();
  json health() const;

private:
  struct TrackState
  {
    ipc::VideoSlotSequenceTracker sequences;
    std::uint64_t received_frames = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t last_frame_ns = 0;
    std::uint64_t activation_published_frames = 0;
    bool previously_available = false;
  };

  void capture_loop();
  bool connect_mapping();
  void disconnect_mapping();
  bool observe_generation();
  void publish_demand(const VideoDemand& demand, std::uint64_t now_ns, bool force);
  void read_track(std::string_view name, const ipc::VideoTrackMapping& mapping, TrackState& state);
  void publish_health(std::string_view name, TrackState& state);

  TrackHandler track_handler_;
  HealthHandler health_handler_;
  DemandProvider demand_provider_;
  GenerationHandler generation_handler_;
  mutable std::mutex mutex_;
  TrackState program_;
  TrackState preview_;
  int mapping_descriptor_ = -1;
  const ipc::VideoFrameMapping* mapping_ = nullptr;
  int control_descriptor_ = -1;
  ipc::VideoCaptureControlMapping* control_ = nullptr;
  bool mapping_connected_ = false;
  std::uint32_t producer_process_id_ = 0;
  VideoDemand current_demand_;
  std::optional<VideoDemand> published_demand_;
  std::optional<std::uint32_t> capture_generation_;
  std::uint64_t demand_generation_ = 0;
  std::uint64_t last_demand_heartbeat_ns_ = 0;
  std::atomic<bool> stopping_{false};
  std::thread thread_;
};
} // namespace studio_remote::linux
