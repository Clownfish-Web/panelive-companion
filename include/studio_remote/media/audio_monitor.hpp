#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace studio_remote
{

struct AudioMonitorFrame
{
  std::uint32_t sample_rate = 0;
  std::uint32_t channels = 0;
  std::uint64_t sequence = 0;
  std::uint64_t first_sample_index = 0;
  std::uint64_t captured_ns = 0;
  std::uint32_t topology_generation = 0;
  std::uint16_t flags = 0;
  std::vector<float> samples;
};

struct AudioMonitorStatus
{
  bool supported = false;
  bool available = false;
  bool configured = false;
  std::string reason;
  std::uint32_t sample_rate = 0;
  std::uint32_t channels = 0;
  std::uint64_t age_ms = 0;
  std::uint64_t dropped_frames = 0;
  std::uint64_t ipc_skipped_frames = 0;
  std::uint64_t timestamp_discontinuities = 0;
  std::uint64_t ingress_overflow_drops = 0;
  std::uint64_t oversized_callback_drops = 0;
  std::uint64_t callback_count = 0;
  std::uint64_t callback_frames_le_256 = 0;
  std::uint64_t callback_frames_le_512 = 0;
  std::uint64_t callback_frames_le_1024 = 0;
  std::uint64_t callback_frames_le_2048 = 0;
  std::uint64_t maximum_ingress_occupancy = 0;
  std::uint64_t mixer_deadline_misses = 0;
  std::uint64_t timeline_gaps = 0;
  std::uint64_t timeline_overlaps = 0;
  std::uint64_t non_finite_samples = 0;
  std::uint64_t limiter_frames = 0;
  std::uint64_t ipc_sample_index_gaps = 0;
  std::uint64_t ipc_seqlock_retries = 0;
  double latest_peak = 0.0;
  double latest_rms = 0.0;
  double latest_dc = 0.0;
};

using SharedAudioMonitorFrame = std::shared_ptr<const AudioMonitorFrame>;

} // namespace studio_remote
