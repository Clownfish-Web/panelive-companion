#pragma once

#include <studio_remote/media/audio_monitor_mix_settings.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace studio_remote::obs_plugin
{

// libobs hands audio-capture callbacks the host input plugin's raw timestamps and applies its own
// 70 ms smoothing window (TS_SMOOTHING_THRESHOLD) only inside its private audio pipeline. WASAPI
// stamps are sample-accurate, but PulseAudio and PipeWire inputs stamp each read callback with the
// wall clock minus the chunk duration, so consecutive callbacks jitter by scheduling latency. The
// mixer therefore applies the same 70 ms window before treating a block as a real discontinuity.
inline constexpr std::int64_t audio_timestamp_smoothing_samples = 3'360;
// Publication delay between a sample's nominal time and its IPC publication. libobs grows its
// own audio buffering when a source delivers late; the mixer mirrors that so a slow host input
// (for example PulseAudio on a remote desktop session) stops producing coverage gaps instead of
// silently losing its late samples. Growth is rounded up to whole 20 ms frames and capped.
inline constexpr std::uint64_t audio_initial_publication_delay_ns = 40'000'000ULL;
inline constexpr std::uint64_t audio_publication_delay_step_ns = 20'000'000ULL;
inline constexpr std::uint64_t audio_maximum_publication_delay_ns = 500'000'000ULL;
inline constexpr std::uint32_t audio_ingress_block_frames = 2'048;
inline constexpr std::uint64_t audio_direct_timestamp_tolerance_ns = 5'000'000'000ULL;
inline constexpr std::uint64_t audio_relative_timestamp_jump_ns = 1'000'000'000ULL;
inline constexpr double audio_monitor_single_source_gain = 1.0;
inline constexpr std::uint32_t audio_monitor_maximum_headroom_db = 12;
inline constexpr std::uint32_t audio_monitor_source_count_hold_frames = 25;
inline constexpr double audio_monitor_gain_recovery_per_frame = 0.03;
inline constexpr double audio_limiter_ceiling = 0.98;
inline constexpr float audio_limiter_release_per_frame = 0.02F;

struct AudioMonitorHeadroomState
{
  std::size_t held_source_count = 0;
  std::uint32_t hold_frames_remaining = 0;
  double gain = audio_monitor_single_source_gain;
};

double audio_monitor_gain_for_source_count(std::size_t active_source_count,
                                           bool automatic_headroom,
                                           std::uint32_t attenuation_db) noexcept;
double update_audio_monitor_headroom(std::size_t active_source_count,
                                     const AudioMonitorMixSettings& settings,
                                     AudioMonitorHeadroomState& state) noexcept;

struct AudioBlockPlacement
{
  std::uint64_t first_sample = 0;
  std::uint32_t source_offset = 0;
  std::uint64_t next_expected_sample = 0;
  std::int64_t timeline_difference = 0;
  bool discontinuity = false;
  bool overlap_repaired = false;
};

struct AudioTimestampNormalizationState
{
  std::uint64_t raw_anchor_ns = 0;
  std::uint64_t normalized_anchor_ns = 0;
  std::uint64_t last_raw_ns = 0;
  bool initialized = false;
  bool source_relative = false;
};

std::uint64_t normalize_audio_timestamp(std::uint64_t raw_timestamp_ns,
                                        std::uint64_t observed_now_ns,
                                        AudioTimestampNormalizationState& state) noexcept;

std::uint32_t audio_ingress_chunk_frames(std::uint32_t callback_frames,
                                         std::uint32_t frame_offset) noexcept;
std::uint64_t audio_timestamp_after_frames(std::uint64_t timestamp_ns,
                                           std::uint32_t frame_offset) noexcept;

// A stalled host read callback delivers a burst of chunks all stamped near "now" while the sample
// stream itself stays continuous, so the deviation from the accumulated timeline can exceed the
// smoothing window for as long as the stall lasted and then swing back. A jump is therefore
// applied only once it persists in the same direction for about half a second of blocks, which is
// longer than any scheduling stall seen on the Linux hosts; a real timeline change still
// re-anchors after that, and a deviation beyond libobs' 2 s MAX_TS_VAR re-anchors immediately.
inline constexpr std::uint32_t audio_timeline_jump_confirmation_blocks = 25;
inline constexpr std::int64_t audio_timeline_reset_samples = 96'000;

struct AudioTimelineJumpState
{
  std::int64_t last_difference = 0;
  std::uint32_t consecutive_blocks = 0;
};

/// Returns true when a deviation beyond the smoothing window has persisted long enough to be a
/// real timeline change rather than callback scheduling jitter.
bool confirm_timeline_jump(AudioTimelineJumpState& state,
                           std::int64_t timeline_difference) noexcept;

/// Places a block, absorbing unconfirmed jumps as contiguous audio.
AudioBlockPlacement place_audio_block(std::uint64_t timestamp_sample,
                                      std::uint64_t expected_next_sample,
                                      std::uint32_t frame_count,
                                      AudioTimelineJumpState& jump_state) noexcept;

/// Publication delay after a block arrived late_samples behind the publication cursor.
std::uint64_t grown_publication_delay_ns(std::uint64_t current_delay_ns,
                                         std::uint64_t late_samples) noexcept;

AudioBlockPlacement place_audio_block(std::uint64_t timestamp_sample,
                                      std::uint64_t expected_next_sample,
                                      std::uint32_t frame_count) noexcept;

double downmix_audio_sample(std::span<const float* const> channels,
                            std::uint32_t frame,
                            bool left) noexcept;

struct AudioPacketSignalMetrics
{
  std::uint64_t non_finite_samples = 0;
  double peak = 0.0;
  double rms = 0.0;
  double dc = 0.0;
  bool limiter_active = false;
};

AudioPacketSignalMetrics
finalize_audio_packet(std::span<double> samples, double common_gain, float& limiter_gain) noexcept;

} // namespace studio_remote::obs_plugin
