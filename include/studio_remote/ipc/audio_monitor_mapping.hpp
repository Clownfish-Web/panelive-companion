#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <studio_remote/media/audio_monitor_mix_settings.hpp>

namespace studio_remote::ipc
{

inline constexpr wchar_t audio_monitor_mapping_name[] =
    L"Local\\panelive-companion.audio-monitor.v3";
inline constexpr char audio_monitor_shared_memory_name[] =
    "/clownfishweb-panelive-audio-monitor-v3";
inline constexpr std::uint32_t audio_monitor_magic = 0x41554D4F;
inline constexpr std::uint32_t audio_monitor_version = 3;
inline constexpr std::uint32_t audio_monitor_sample_rate = 48'000;
inline constexpr std::uint32_t audio_monitor_maximum_channels = 2;
inline constexpr std::uint32_t audio_monitor_frame_samples = 960;
inline constexpr std::size_t audio_monitor_slot_count = 16;
inline constexpr wchar_t audio_monitor_control_mapping_name[] =
    L"Local\\panelive-companion.audio-monitor-control.v2";
inline constexpr char audio_monitor_control_shared_memory_name[] =
    "/clownfishweb-panelive-audio-monitor-control-v2";
inline constexpr std::uint32_t audio_monitor_control_magic = 0x41554D43;
inline constexpr std::uint32_t audio_monitor_control_version = 2;

enum class AudioMonitorFrameFlags : std::uint16_t
{
  none = 0,
  gap = 1U << 0U,
  overlap_repaired = 1U << 1U,
  limiter_active = 1U << 2U,
  discontinuity = 1U << 3U
};

enum class AudioMonitorSampleFormat : std::uint32_t
{
  float32_interleaved = 1
};

struct AudioMonitorFrameSlot
{
  alignas(8) std::atomic<std::uint64_t> sequence{0};
  std::uint64_t first_sample_index = 0;
  std::uint64_t captured_ns = 0;
  std::uint32_t topology_generation = 0;
  std::uint16_t sample_rate = 0;
  std::uint16_t channels = 0;
  std::uint16_t frame_count = 0;
  AudioMonitorFrameFlags flags = AudioMonitorFrameFlags::none;
  float samples[audio_monitor_frame_samples * audio_monitor_maximum_channels]{};
};

struct AudioMonitorMapping
{
  std::uint32_t magic = audio_monitor_magic;
  std::uint32_t version = audio_monitor_version;
  std::uint32_t structure_size = 0;
  std::uint32_t producer_process_id = 0;
  alignas(8) std::atomic<std::uint64_t> heartbeat_ns{0};
  std::uint32_t sample_rate = audio_monitor_sample_rate;
  std::atomic<std::uint32_t> channels{0};
  AudioMonitorSampleFormat sample_format = AudioMonitorSampleFormat::float32_interleaved;
  std::atomic<std::uint32_t> configured{0};
  AudioMonitorFrameSlot slots[audio_monitor_slot_count]{};
  alignas(8) std::atomic<std::uint64_t> published_sequence{0};
  alignas(8) std::atomic<std::uint64_t> dropped_frames{0};
  alignas(8) std::atomic<std::uint64_t> ingress_overflow_drops{0};
  alignas(8) std::atomic<std::uint64_t> oversized_callback_drops{0};
  alignas(8) std::atomic<std::uint64_t> callback_count{0};
  alignas(8) std::atomic<std::uint64_t> callback_frames_le_256{0};
  alignas(8) std::atomic<std::uint64_t> callback_frames_le_512{0};
  alignas(8) std::atomic<std::uint64_t> callback_frames_le_1024{0};
  alignas(8) std::atomic<std::uint64_t> callback_frames_le_2048{0};
  alignas(8) std::atomic<std::uint64_t> maximum_ingress_occupancy{0};
  alignas(8) std::atomic<std::uint64_t> mixer_deadline_misses{0};
  alignas(8) std::atomic<std::uint64_t> timeline_gaps{0};
  alignas(8) std::atomic<std::uint64_t> timeline_overlaps{0};
  alignas(8) std::atomic<std::uint64_t> timestamp_discontinuities{0};
  alignas(8) std::atomic<std::uint64_t> non_finite_samples{0};
  alignas(8) std::atomic<std::uint64_t> limiter_frames{0};
  alignas(8) std::atomic<std::uint64_t> latest_peak_millionths{0};
  alignas(8) std::atomic<std::uint64_t> latest_rms_millionths{0};
  alignas(8) std::atomic<std::int64_t> latest_dc_millionths{0};
};

struct AudioMonitorControlMapping
{
  std::uint32_t magic = audio_monitor_control_magic;
  std::uint32_t version = audio_monitor_control_version;
  std::uint32_t structure_size = 0;
  std::uint32_t producer_process_id = 0;
  std::atomic<std::uint32_t> automatic_headroom{1};
  std::atomic<std::uint32_t> attenuation_per_additional_source_db{
      default_audio_monitor_headroom_db};
  std::atomic<std::uint32_t> activate_audio_source_audition_bus{0};
};

inline bool compatible_audio_monitor_control(const AudioMonitorControlMapping& control) noexcept
{
  return control.magic == audio_monitor_control_magic &&
         control.version == audio_monitor_control_version &&
         control.structure_size == sizeof(AudioMonitorControlMapping);
}

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

} // namespace studio_remote::ipc
