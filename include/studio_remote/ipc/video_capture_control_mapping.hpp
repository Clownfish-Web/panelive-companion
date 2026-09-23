#pragma once

#include <atomic>
#include <cstdint>

#include <studio_remote/media/video_demand.hpp>

namespace studio_remote::ipc
{

inline constexpr wchar_t video_capture_control_mapping_name[] =
    L"Local\\panelive-companion.video-capture-control.v2";
inline constexpr char video_capture_control_shared_memory_name[] =
    "/clownfishweb-panelive-video-capture-control-v2";
inline constexpr std::uint32_t video_capture_control_magic = 0x56434354;
inline constexpr std::uint32_t video_capture_control_version = 2;
inline constexpr std::uint64_t video_capture_demand_heartbeat_interval_ns = 250'000'000ULL;
inline constexpr std::uint64_t video_capture_demand_timeout_ns = 2'000'000'000ULL;
inline constexpr std::uint32_t maximum_requested_video_fps = 15;

struct VideoCaptureControlMapping
{
  std::uint32_t magic = video_capture_control_magic;
  std::uint32_t version = video_capture_control_version;
  std::uint32_t structure_size = 0;
  std::uint32_t producer_process_id = 0;
  alignas(8) std::atomic<std::uint64_t> demand_generation{0};
  alignas(4) std::atomic<std::uint32_t> active_video_client_count{0};
  alignas(4) std::atomic<std::uint32_t> requested_program_fps{0};
  alignas(4) std::atomic<std::uint32_t> requested_preview_fps{0};
  alignas(8) std::atomic<std::uint64_t> companion_heartbeat_ns{0};
};

struct VideoCaptureDemandSnapshot
{
  std::uint64_t generation = 0;
  VideoDemand demand;
  std::uint64_t heartbeat_ns = 0;
  bool heartbeat_fresh = false;
};

inline bool video_capture_control_is_compatible(const VideoCaptureControlMapping& mapping) noexcept
{
  return mapping.magic == video_capture_control_magic &&
         mapping.version == video_capture_control_version &&
         mapping.structure_size == sizeof(VideoCaptureControlMapping);
}

inline VideoCaptureDemandSnapshot
read_video_capture_demand(const VideoCaptureControlMapping& mapping, std::uint64_t now_ns) noexcept
{
  VideoCaptureDemandSnapshot snapshot;
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    const auto generation_before = mapping.demand_generation.load(std::memory_order_acquire);
    if ((generation_before & 1U) != 0)
      continue;

    snapshot.demand.active_video_client_count =
        mapping.active_video_client_count.load(std::memory_order_relaxed);
    snapshot.demand.requested_program_fps =
        mapping.requested_program_fps.load(std::memory_order_relaxed);
    snapshot.demand.requested_preview_fps =
        mapping.requested_preview_fps.load(std::memory_order_relaxed);
    snapshot.heartbeat_ns = mapping.companion_heartbeat_ns.load(std::memory_order_acquire);
    const auto generation_after = mapping.demand_generation.load(std::memory_order_acquire);
    if (generation_before != generation_after || (generation_after & 1U) != 0)
      continue;

    snapshot.generation = generation_after;
    snapshot.heartbeat_fresh = snapshot.heartbeat_ns != 0 && now_ns >= snapshot.heartbeat_ns &&
                               now_ns - snapshot.heartbeat_ns <= video_capture_demand_timeout_ns;
    if (!snapshot.heartbeat_fresh)
      snapshot.demand = {};
    return snapshot;
  }
  return {};
}

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

} // namespace studio_remote::ipc
