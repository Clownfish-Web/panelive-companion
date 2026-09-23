#pragma once

#include "obs_audio_mix_policy.hpp"

#include <studio_remote/media/fixed_spsc_ring.hpp>

#include <obs.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace studio_remote::obs_plugin
{

inline constexpr std::size_t audio_ingress_maximum_channels = 8;
inline constexpr std::size_t audio_ingress_capacity = 8;
inline constexpr std::size_t audio_ingress_maximum_callback_frames =
    audio_ingress_block_frames * audio_ingress_capacity;

struct ObsAudioIngressBlock
{
  std::uint64_t callback_timestamp_ns = 0;
  std::int64_t sync_offset_ns = 0;
  std::uint32_t frame_count = 0;
  std::uint16_t channels = 0;
  bool muted = false;
  float volume = 0.0F;
  std::uint32_t topology_generation = 0;
  std::array<std::array<float, audio_ingress_block_frames>, audio_ingress_maximum_channels>
      planar{};
};

struct ObsAudioIngressCounters
{
  std::atomic<std::uint64_t> overflow_drops{0};
  std::atomic<std::uint64_t> oversized_callback_drops{0};
  std::atomic<std::uint64_t> callback_count{0};
  std::array<std::atomic<std::uint64_t>, 4> callback_frame_histogram{};
  std::atomic<std::uint64_t> maximum_occupancy{0};
};

// One OBS source owns one callback producer and one mixer-thread consumer. Callback storage and
// counters are fixed for the complete subscribed lifetime.
class ObsAudioSourceIngress
{
public:
  ObsAudioSourceIngress(obs_source_t* source,
                        std::uint16_t channels,
                        ObsAudioIngressCounters& counters);
  ~ObsAudioSourceIngress();

  ObsAudioSourceIngress(const ObsAudioSourceIngress&) = delete;
  ObsAudioSourceIngress& operator=(const ObsAudioSourceIngress&) = delete;

  void attach();
  void detach();
  void set_audition_activation(bool enabled) noexcept;
  void update_control(float volume,
                      std::int64_t sync_offset_ns,
                      std::uint32_t topology_generation) noexcept;
  const ObsAudioIngressBlock* try_begin_read() const noexcept;
  void commit_read() noexcept;
  obs_source_t* source() const noexcept;
  std::uint64_t expected_next_sample() const noexcept;
  void set_expected_next_sample(std::uint64_t sample) noexcept;
  AudioTimelineJumpState& timeline_jump_state() noexcept;
  bool mark_packet_contribution(std::uint64_t first_sample) noexcept;
  std::uint64_t normalize_timestamp(std::uint64_t raw_timestamp_ns,
                                    std::uint64_t observed_now_ns) noexcept;

private:
  static void
  on_audio(void* context, obs_source_t* source, const audio_data* audio, bool muted) noexcept;

  obs_source_t* source_ = nullptr;
  std::uint16_t channels_ = 0;
  ObsAudioIngressCounters& counters_;
  FixedSpscRing<ObsAudioIngressBlock, audio_ingress_capacity> ring_;
  std::atomic<bool> enabled_{false};
  bool audition_activation_held_ = false;
  std::atomic<float> volume_{1.0F};
  std::atomic<std::int64_t> sync_offset_ns_{0};
  std::atomic<std::uint32_t> topology_generation_{0};
  std::uint64_t expected_next_sample_ = 0;
  AudioTimelineJumpState timeline_jump_state_;
  std::uint64_t last_contribution_packet_ = 0;
  bool has_contribution_packet_ = false;
  AudioTimestampNormalizationState timestamp_normalization_;
};

} // namespace studio_remote::obs_plugin
