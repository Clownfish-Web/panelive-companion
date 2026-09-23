#include "obs_audio_source_ingress.hpp"

#include <algorithm>
#include <cstring>

namespace studio_remote::obs_plugin
{

ObsAudioSourceIngress::ObsAudioSourceIngress(obs_source_t* source,
                                             std::uint16_t channels,
                                             ObsAudioIngressCounters& counters)
    : source_(source), channels_(channels), counters_(counters)
{
}

ObsAudioSourceIngress::~ObsAudioSourceIngress()
{
  detach();
}

void ObsAudioSourceIngress::attach()
{
  if (!source_ || enabled_.exchange(true, std::memory_order_acq_rel))
    return;
  obs_source_add_audio_capture_callback(source_, on_audio, this);
}

void ObsAudioSourceIngress::detach()
{
  enabled_.store(false, std::memory_order_release);
  if (!source_)
    return;
  obs_source_remove_audio_capture_callback(source_, on_audio, this);
  set_audition_activation(false);
  obs_source_release(source_);
  source_ = nullptr;
}

void ObsAudioSourceIngress::set_audition_activation(bool enabled) noexcept
{
  if (!source_ || enabled == audition_activation_held_)
    return;

  // This opt-in lease changes source activity only. It never inserts the source into the Program
  // scene tree or changes OBS output routing, so Preview-only audio cannot enter the output mix.
  if (enabled)
    obs_source_inc_active(source_);
  else
    obs_source_dec_active(source_);
  audition_activation_held_ = enabled;
}

void ObsAudioSourceIngress::update_control(float volume,
                                           std::int64_t sync_offset_ns,
                                           std::uint32_t topology_generation) noexcept
{
  volume_.store(volume, std::memory_order_relaxed);
  sync_offset_ns_.store(sync_offset_ns, std::memory_order_relaxed);
  topology_generation_.store(topology_generation, std::memory_order_relaxed);
}

const ObsAudioIngressBlock* ObsAudioSourceIngress::try_begin_read() const noexcept
{
  return ring_.try_begin_read();
}

void ObsAudioSourceIngress::commit_read() noexcept
{
  ring_.commit_read();
}

obs_source_t* ObsAudioSourceIngress::source() const noexcept
{
  return source_;
}

std::uint64_t ObsAudioSourceIngress::expected_next_sample() const noexcept
{
  return expected_next_sample_;
}

void ObsAudioSourceIngress::set_expected_next_sample(std::uint64_t sample) noexcept
{
  expected_next_sample_ = sample;
}

AudioTimelineJumpState& ObsAudioSourceIngress::timeline_jump_state() noexcept
{
  return timeline_jump_state_;
}

bool ObsAudioSourceIngress::mark_packet_contribution(std::uint64_t first_sample) noexcept
{
  if (has_contribution_packet_ && last_contribution_packet_ == first_sample)
    return false;
  last_contribution_packet_ = first_sample;
  has_contribution_packet_ = true;
  return true;
}

std::uint64_t ObsAudioSourceIngress::normalize_timestamp(std::uint64_t raw_timestamp_ns,
                                                         std::uint64_t observed_now_ns) noexcept
{
  return normalize_audio_timestamp(raw_timestamp_ns, observed_now_ns, timestamp_normalization_);
}

void ObsAudioSourceIngress::on_audio(void* context,
                                     obs_source_t*,
                                     const audio_data* audio,
                                     bool muted) noexcept
{
  if (!context || !audio || audio->frames == 0)
    return;
  auto& ingress = *static_cast<ObsAudioSourceIngress*>(context);
  if (!ingress.enabled_.load(std::memory_order_relaxed))
    return;
  ingress.counters_.callback_count.fetch_add(1, std::memory_order_relaxed);
  if (audio->frames > audio_ingress_maximum_callback_frames)
  {
    ingress.counters_.oversized_callback_drops.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (audio->frames <= audio_ingress_block_frames)
  {
    const std::size_t histogram_index = audio->frames <= 256     ? 0
                                        : audio->frames <= 512   ? 1
                                        : audio->frames <= 1'024 ? 2
                                                                 : 3;
    ingress.counters_.callback_frame_histogram[histogram_index].fetch_add(
        1, std::memory_order_relaxed);
  }
  for (std::uint32_t frame_offset = 0; frame_offset < audio->frames;)
  {
    auto* block = ingress.ring_.try_reserve_write();
    if (!block)
    {
      ingress.counters_.overflow_drops.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    const auto chunk_frames = audio_ingress_chunk_frames(audio->frames, frame_offset);
    block->callback_timestamp_ns = audio_timestamp_after_frames(audio->timestamp, frame_offset);
    block->sync_offset_ns = ingress.sync_offset_ns_.load(std::memory_order_relaxed);
    block->frame_count = chunk_frames;
    block->channels = ingress.channels_;
    block->muted = muted;
    block->volume = ingress.volume_.load(std::memory_order_relaxed);
    block->topology_generation = ingress.topology_generation_.load(std::memory_order_relaxed);
    for (std::uint32_t channel = 0; channel < block->channels; ++channel)
    {
      auto* destination = block->planar[channel].data();
      if (audio->data[channel])
      {
        const auto* source = reinterpret_cast<const float*>(audio->data[channel]) + frame_offset;
        std::memcpy(destination, source, chunk_frames * sizeof(float));
      }
      else
      {
        std::fill_n(destination, chunk_frames, 0.0F);
      }
    }
    ingress.ring_.commit_write();
    const auto occupancy = static_cast<std::uint64_t>(ingress.ring_.size());
    auto maximum = ingress.counters_.maximum_occupancy.load(std::memory_order_relaxed);
    while (occupancy > maximum && !ingress.counters_.maximum_occupancy.compare_exchange_weak(
                                      maximum, occupancy, std::memory_order_relaxed))
    {
    }
    frame_offset += chunk_frames;
  }
}

} // namespace studio_remote::obs_plugin
