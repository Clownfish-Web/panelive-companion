#pragma once

#include <cstdint>
#include <optional>

namespace studio_remote
{

inline constexpr std::uint32_t performance_update_interval_ms = 2'000;
inline constexpr std::uint32_t performance_stale_after_ms = 6'000;

struct ProductionPerformanceSample
{
  std::optional<double> cpu_usage_percent;
  std::optional<double> memory_usage_mib;
  std::optional<double> available_disk_space_mib;
  std::optional<double> frames_per_second;
  std::optional<double> average_frame_time_ms;
  std::optional<std::uint64_t> render_skipped_frames;
  std::optional<std::uint64_t> render_total_frames;
  std::optional<std::uint64_t> encode_skipped_frames;
  std::optional<std::uint64_t> encode_total_frames;
  std::optional<double> stream_bitrate_kbps;
  std::optional<double> stream_congestion_ratio;
  std::optional<std::uint64_t> stream_dropped_frames;
  std::optional<double> stream_dropped_frames_percent;
  std::optional<std::uint64_t> stream_bytes;
  std::optional<std::uint64_t> stream_duration_ms;
};

} // namespace studio_remote
