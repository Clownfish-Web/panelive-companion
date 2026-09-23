#include "obs_audio_mix_policy.hpp"

#include <studio_remote/ipc/audio_monitor_mapping.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace studio_remote::obs_plugin
{
namespace
{
std::uint64_t timestamp_distance(std::uint64_t left, std::uint64_t right) noexcept
{
  return left > right ? left - right : right - left;
}

std::uint64_t add_timestamp_delta(std::uint64_t anchor, std::uint64_t delta) noexcept
{
  return delta > std::numeric_limits<std::uint64_t>::max() - anchor
             ? std::numeric_limits<std::uint64_t>::max()
             : anchor + delta;
}
} // namespace

double audio_monitor_gain_for_source_count(std::size_t active_source_count,
                                           bool automatic_headroom,
                                           std::uint32_t attenuation_db) noexcept
{
  if (!automatic_headroom || active_source_count <= 1)
    return audio_monitor_single_source_gain;
  const auto valid_attenuation_db = std::clamp(attenuation_db,
                                               audio_monitor_headroom_presets_db.front(),
                                               audio_monitor_headroom_presets_db.back());
  const auto additional_sources = static_cast<std::uint64_t>(active_source_count - 1);
  const auto headroom_db = std::min<std::uint64_t>(audio_monitor_maximum_headroom_db,
                                                   additional_sources * valid_attenuation_db);
  return std::pow(10.0, -static_cast<double>(headroom_db) / 20.0);
}

double update_audio_monitor_headroom(std::size_t active_source_count,
                                     const AudioMonitorMixSettings& settings,
                                     AudioMonitorHeadroomState& state) noexcept
{
  if (!settings.automatic_headroom)
  {
    state.held_source_count = active_source_count;
    state.hold_frames_remaining = 0;
    state.gain = audio_monitor_single_source_gain;
    return state.gain;
  }
  else if (active_source_count >= state.held_source_count)
  {
    state.held_source_count = active_source_count;
    state.hold_frames_remaining = audio_monitor_source_count_hold_frames;
  }
  else if (state.hold_frames_remaining > 0)
  {
    --state.hold_frames_remaining;
  }
  else
  {
    state.held_source_count = active_source_count;
  }

  const auto target =
      audio_monitor_gain_for_source_count(state.held_source_count,
                                          settings.automatic_headroom,
                                          settings.attenuation_per_additional_source_db);
  if (target < state.gain)
    state.gain = target;
  else
    state.gain = std::min(target, state.gain + audio_monitor_gain_recovery_per_frame);
  return state.gain;
}

std::uint64_t normalize_audio_timestamp(std::uint64_t raw_timestamp_ns,
                                        std::uint64_t observed_now_ns,
                                        AudioTimestampNormalizationState& state) noexcept
{
  const bool direct =
      timestamp_distance(raw_timestamp_ns, observed_now_ns) <= audio_direct_timestamp_tolerance_ns;
  if (direct)
  {
    state.last_raw_ns = raw_timestamp_ns;
    state.initialized = true;
    state.source_relative = false;
    return raw_timestamp_ns;
  }

  const bool relative_continuity =
      state.initialized && state.source_relative && raw_timestamp_ns >= state.last_raw_ns &&
      raw_timestamp_ns - state.last_raw_ns <= audio_relative_timestamp_jump_ns;
  if (!relative_continuity)
  {
    state.raw_anchor_ns = raw_timestamp_ns;
    state.normalized_anchor_ns = observed_now_ns;
    state.initialized = true;
    state.source_relative = true;
  }
  state.last_raw_ns = raw_timestamp_ns;
  return add_timestamp_delta(state.normalized_anchor_ns, raw_timestamp_ns - state.raw_anchor_ns);
}

std::uint32_t audio_ingress_chunk_frames(std::uint32_t callback_frames,
                                         std::uint32_t frame_offset) noexcept
{
  if (frame_offset >= callback_frames)
    return 0;
  return std::min(audio_ingress_block_frames, callback_frames - frame_offset);
}

std::uint64_t audio_timestamp_after_frames(std::uint64_t timestamp_ns,
                                           std::uint32_t frame_offset) noexcept
{
  constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
  const auto seconds = frame_offset / ipc::audio_monitor_sample_rate;
  const auto remainder = frame_offset % ipc::audio_monitor_sample_rate;
  const auto delta = seconds * nanoseconds_per_second +
                     remainder * nanoseconds_per_second / ipc::audio_monitor_sample_rate;
  return add_timestamp_delta(timestamp_ns, delta);
}

std::uint64_t grown_publication_delay_ns(std::uint64_t current_delay_ns,
                                         std::uint64_t late_samples) noexcept
{
  if (late_samples == 0)
    return current_delay_ns;
  constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
  const auto late_ns = late_samples * nanoseconds_per_second / ipc::audio_monitor_sample_rate;
  const auto steps =
      (late_ns + audio_publication_delay_step_ns - 1) / audio_publication_delay_step_ns;
  const auto grown = current_delay_ns + steps * audio_publication_delay_step_ns;
  return std::min(grown, audio_maximum_publication_delay_ns);
}

AudioBlockPlacement place_audio_block(std::uint64_t timestamp_sample,
                                      std::uint64_t expected_next_sample,
                                      std::uint32_t frame_count) noexcept
{
  AudioBlockPlacement result;
  result.first_sample = timestamp_sample;
  if (expected_next_sample != 0)
  {
    result.timeline_difference = static_cast<std::int64_t>(timestamp_sample) -
                                 static_cast<std::int64_t>(expected_next_sample);
    if (std::abs(result.timeline_difference) <= audio_timestamp_smoothing_samples)
      result.first_sample = expected_next_sample;
    else
      result.discontinuity = true;
    if (result.first_sample < expected_next_sample)
    {
      const auto overlap = expected_next_sample - result.first_sample;
      result.source_offset =
          static_cast<std::uint32_t>(std::min<std::uint64_t>(overlap, frame_count));
      result.first_sample += result.source_offset;
      result.overlap_repaired = result.source_offset != 0;
    }
  }
  result.next_expected_sample =
      result.first_sample + static_cast<std::uint64_t>(frame_count - result.source_offset);
  return result;
}

bool confirm_timeline_jump(AudioTimelineJumpState& state, std::int64_t timeline_difference) noexcept
{
  if (std::abs(timeline_difference) <= audio_timestamp_smoothing_samples)
  {
    state = {};
    return false;
  }
  if (std::abs(timeline_difference) > audio_timeline_reset_samples)
  {
    state = {};
    return true;
  }
  const bool same_direction =
      state.consecutive_blocks != 0 && (state.last_difference < 0) == (timeline_difference < 0);
  state.consecutive_blocks = same_direction ? state.consecutive_blocks + 1 : 1;
  state.last_difference = timeline_difference;
  if (state.consecutive_blocks < audio_timeline_jump_confirmation_blocks)
    return false;
  state = {};
  return true;
}

AudioBlockPlacement place_audio_block(std::uint64_t timestamp_sample,
                                      std::uint64_t expected_next_sample,
                                      std::uint32_t frame_count,
                                      AudioTimelineJumpState& jump_state) noexcept
{
  const auto placement = place_audio_block(timestamp_sample, expected_next_sample, frame_count);
  if (!placement.discontinuity || confirm_timeline_jump(jump_state, placement.timeline_difference))
    return placement;
  return place_audio_block(expected_next_sample, expected_next_sample, frame_count);
}

double downmix_audio_sample(std::span<const float* const> channels,
                            std::uint32_t frame,
                            bool left) noexcept
{
  const auto sample = [&](std::size_t channel)
  { return channel < channels.size() && channels[channel] ? channels[channel][frame] : 0.0F; };
  if (channels.size() == 1)
    return sample(0);
  double value = sample(left ? 0U : 1U);
  if (channels.size() > 2)
    value += sample(2) * 0.70710678;
  if (channels.size() > 3)
    value += sample(3) * 0.5;
  if (channels.size() > 4)
    value += sample(left ? 4U : 5U) * 0.70710678;
  if (channels.size() > 6)
    value += sample(left ? 6U : 7U) * 0.70710678;
  return value;
}

AudioPacketSignalMetrics
finalize_audio_packet(std::span<double> samples, double common_gain, float& limiter_gain) noexcept
{
  AudioPacketSignalMetrics result;
  common_gain = std::clamp(common_gain, 0.0, 1.0);
  for (auto& sample : samples)
  {
    if (!std::isfinite(sample))
    {
      sample = 0.0;
      ++result.non_finite_samples;
    }
    sample *= common_gain;
    result.peak = std::max(result.peak, std::abs(sample));
  }
  const auto required_gain =
      result.peak > audio_limiter_ceiling ? audio_limiter_ceiling / result.peak : 1.0;
  if (required_gain < limiter_gain)
  {
    limiter_gain = static_cast<float>(required_gain);
    if (static_cast<double>(limiter_gain) * result.peak > audio_limiter_ceiling)
      limiter_gain = std::nextafter(limiter_gain, 0.0F);
  }
  else
    limiter_gain = std::min(1.0F, limiter_gain + audio_limiter_release_per_frame);
  result.limiter_active = limiter_gain < 1.0F;

  double sum = 0.0;
  double sum_squares = 0.0;
  for (auto& sample : samples)
  {
    sample *= limiter_gain;
    sum += sample;
    sum_squares += sample * sample;
  }
  if (!samples.empty())
  {
    const auto count = static_cast<double>(samples.size());
    result.peak *= limiter_gain;
    result.rms = std::sqrt(sum_squares / count);
    result.dc = sum / count;
  }
  return result;
}

} // namespace studio_remote::obs_plugin
