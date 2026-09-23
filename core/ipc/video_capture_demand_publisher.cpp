#include <studio_remote/ipc/video_capture_demand_publisher.hpp>

#include <studio_remote/ipc/video_capture_control_mapping.hpp>

#include <algorithm>

namespace studio_remote::ipc
{
std::optional<VideoCaptureDemandPublisher>
VideoCaptureDemandPublisher::open(const VideoCaptureControlMapping& mapping,
                                  std::optional<std::uint32_t> expected_producer) noexcept
{
  if (!video_capture_control_is_compatible(mapping) || mapping.producer_process_id == 0 ||
      (expected_producer && mapping.producer_process_id != *expected_producer))
    return std::nullopt;
  return VideoCaptureDemandPublisher(mapping.producer_process_id,
                                     mapping.demand_generation.load(std::memory_order_acquire));
}

VideoCaptureDemandPublisher::VideoCaptureDemandPublisher(std::uint32_t producer,
                                                         std::uint64_t generation) noexcept
    : generation_(generation), producer_process_id_(producer)
{
  if ((generation_ & 1U) != 0)
    ++generation_;
}

bool VideoCaptureDemandPublisher::publish(VideoCaptureControlMapping& mapping,
                                          VideoDemand demand,
                                          std::uint64_t now_ns) noexcept
{
  return publish(mapping, demand, now_ns, false);
}

bool VideoCaptureDemandPublisher::publish_idle(VideoCaptureControlMapping& mapping,
                                               std::uint64_t now_ns) noexcept
{
  return publish(mapping, {}, now_ns, true);
}

bool VideoCaptureDemandPublisher::publish(VideoCaptureControlMapping& mapping,
                                          VideoDemand demand,
                                          std::uint64_t now_ns,
                                          bool force_heartbeat) noexcept
{
  if (!video_capture_control_is_compatible(mapping) ||
      mapping.producer_process_id != producer_process_id_)
    return false;
  demand.requested_program_fps =
      std::min(demand.requested_program_fps, maximum_requested_video_fps);
  demand.requested_preview_fps =
      std::min(demand.requested_preview_fps, maximum_requested_video_fps);
  if (demand.active_video_client_count == 0)
    demand = {};
  if (!published_ || *published_ != demand)
  {
    mapping.demand_generation.store(generation_ + 1, std::memory_order_release);
    mapping.active_video_client_count.store(demand.active_video_client_count,
                                            std::memory_order_relaxed);
    mapping.requested_program_fps.store(demand.requested_program_fps, std::memory_order_relaxed);
    mapping.requested_preview_fps.store(demand.requested_preview_fps, std::memory_order_relaxed);
    generation_ += 2;
    mapping.demand_generation.store(generation_, std::memory_order_release);
    published_ = demand;
    force_heartbeat = true;
  }
  if (force_heartbeat || heartbeat_ns_ == 0 || now_ns < heartbeat_ns_ ||
      now_ns - heartbeat_ns_ >= video_capture_demand_heartbeat_interval_ns)
  {
    mapping.companion_heartbeat_ns.store(now_ns, std::memory_order_release);
    heartbeat_ns_ = now_ns;
  }
  return true;
}

std::uint64_t VideoCaptureDemandPublisher::generation() const noexcept
{
  return generation_;
}

std::uint32_t VideoCaptureDemandPublisher::producer_process_id() const noexcept
{
  return producer_process_id_;
}
} // namespace studio_remote::ipc
