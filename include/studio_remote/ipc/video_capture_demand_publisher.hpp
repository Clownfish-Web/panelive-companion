#pragma once

#include <studio_remote/media/video_demand.hpp>

#include <cstdint>
#include <optional>

namespace studio_remote::ipc
{
struct VideoCaptureControlMapping;

/// One capture resource owns this publisher. It borrows the same producer's control mapping for
/// each synchronous publication; native mapping ownership, clock and final idle publication stay
/// with that resource. No thread may publish through it concurrently.
class VideoCaptureDemandPublisher final
{
public:
  static std::optional<VideoCaptureDemandPublisher>
  open(const VideoCaptureControlMapping& mapping,
       std::optional<std::uint32_t> expected_producer = std::nullopt) noexcept;

  /// Returns false without stores when the borrowed mapping no longer has the adopted identity.
  bool
  publish(VideoCaptureControlMapping& mapping, VideoDemand demand, std::uint64_t now_ns) noexcept;
  bool publish_idle(VideoCaptureControlMapping& mapping, std::uint64_t now_ns) noexcept;
  std::uint64_t generation() const noexcept;
  std::uint32_t producer_process_id() const noexcept;

private:
  VideoCaptureDemandPublisher(std::uint32_t producer, std::uint64_t generation) noexcept;
  bool publish(VideoCaptureControlMapping& mapping,
               VideoDemand demand,
               std::uint64_t now_ns,
               bool force_heartbeat) noexcept;

  std::optional<VideoDemand> published_;
  std::uint64_t generation_;
  std::uint64_t heartbeat_ns_ = 0;
  std::uint32_t producer_process_id_;
};
} // namespace studio_remote::ipc
