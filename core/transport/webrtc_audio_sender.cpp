#include "libdatachannel_peer.hpp"
#include "libdatachannel_transport.hpp"

#include <studio_remote/ipc/audio_monitor_mapping.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <opus/opus.h>
#include <thread>

namespace studio_remote
{
namespace
{
class RecentLatencyWindow
{
public:
  std::uint64_t observe(std::uint64_t microseconds) noexcept
  {
    values_[position_] = microseconds;
    position_ = (position_ + 1) % values_.size();
    count_ = std::min(count_ + 1, values_.size());
    auto ordered = values_;
    std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(count_));
    const auto percentile_index = count_ == 0 ? 0 : (count_ - 1) * 99 / 100;
    return ordered[percentile_index];
  }

  std::uint64_t percentile(std::size_t percent) const noexcept
  {
    if (count_ == 0)
      return 0;
    auto ordered = values_;
    std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(count_));
    return ordered[(count_ - 1) * percent / 100];
  }

private:
  std::array<std::uint64_t, 50> values_{};
  std::size_t position_ = 0;
  std::size_t count_ = 0;
};
} // namespace

void WebRtcHub::audio_sender_loop()
{
  OpusDecoder* diagnostic_decoder = nullptr;
  auto registration = realtime_audio_
                          ? realtime_audio_->register_current_thread(RealtimeAudioRole::rtp_sender)
                          : nullptr;
  if (registration && !registration->active())
    audio_sender_mmcss_failures_.fetch_add(1, std::memory_order_relaxed);
  const auto frequency = realtime_audio_ ? realtime_audio_->performance_frequency() : 0;
  const auto frame_ticks = frequency == 0 ? 0 : frequency / 50;
  auto deadline_ticks = performance_counter();
  auto fallback_deadline = std::chrono::steady_clock::now();
  bool pacing_started = false;
  while (!stopping_)
  {
    {
      std::unique_lock lock(mutex_);
      frame_cv_.wait_for(lock,
                         std::chrono::milliseconds(100),
                         [&] { return stopping_ || encoded_audio_frames_.size() != 0; });
    }
    if (stopping_)
      break;

    const auto now_ticks = performance_counter();
    auto encoded = encoded_audio_frames_.pop();
    if (!encoded)
      continue;
    if (!pacing_started)
    {
      deadline_ticks = now_ticks;
      fallback_deadline = std::chrono::steady_clock::now();
      pacing_started = true;
    }
    else if (frame_ticks != 0 && now_ticks > deadline_ticks + frame_ticks * 2)
    {
      deadline_ticks = now_ticks;
      audio_pacing_resets_.fetch_add(1, std::memory_order_relaxed);
    }
    else if (frame_ticks == 0 &&
             std::chrono::steady_clock::now() > fallback_deadline + std::chrono::milliseconds(40))
    {
      fallback_deadline = std::chrono::steady_clock::now();
      audio_pacing_resets_.fetch_add(1, std::memory_order_relaxed);
    }

    if (frame_ticks != 0)
    {
      const auto before_wait = performance_counter();
      if (deadline_ticks > before_wait)
      {
        if (!realtime_audio_ || !realtime_audio_->wait_until(deadline_ticks))
        {
          const auto wait_us = performance_counter_us(deadline_ticks - before_wait);
          std::this_thread::sleep_for(std::chrono::microseconds(wait_us));
        }
      }
      const auto sent_at = performance_counter();
      if (sent_at > deadline_ticks)
      {
        const auto lateness = performance_counter_us(sent_at - deadline_ticks);
        if (lateness > 1'000)
          audio_pacing_late_frames_.fetch_add(1, std::memory_order_relaxed);
        auto maximum = audio_maximum_pacing_lateness_us_.load(std::memory_order_relaxed);
        while (lateness > maximum && !audio_maximum_pacing_lateness_us_.compare_exchange_weak(
                                         maximum, lateness, std::memory_order_relaxed))
        {
        }
      }
      deadline_ticks += frame_ticks;
    }
    else
    {
      if (fallback_deadline > std::chrono::steady_clock::now())
        std::this_thread::sleep_until(fallback_deadline);
      fallback_deadline += std::chrono::milliseconds(20);
    }

    if (post_opus_diagnostic_enabled_.load(std::memory_order_acquire))
    {
      if (!diagnostic_decoder)
      {
        int error = OPUS_OK;
        diagnostic_decoder = opus_decoder_create(48'000, 2, &error);
        if (!diagnostic_decoder || error != OPUS_OK)
        {
          diagnostic_decoder = nullptr;
          post_opus_diagnostic_decode_failures_.fetch_add(1, std::memory_order_relaxed);
        }
      }
      if (diagnostic_decoder)
      {
        auto decoded = std::make_shared<AudioMonitorFrame>();
        decoded->sample_rate = ipc::audio_monitor_sample_rate;
        decoded->channels = 2;
        decoded->sequence = encoded->source_sequence;
        decoded->first_sample_index = encoded->first_sample_index;
        decoded->topology_generation = encoded->topology_generation;
        decoded->flags = encoded->flags;
        decoded->samples.resize(ipc::audio_monitor_frame_samples * 2);
        const auto decoded_frames =
            opus_decode_float(diagnostic_decoder,
                              reinterpret_cast<const unsigned char*>(encoded->payload.data()),
                              encoded->payload_bytes,
                              decoded->samples.data(),
                              ipc::audio_monitor_frame_samples,
                              0);
        if (decoded_frames == static_cast<int>(ipc::audio_monitor_frame_samples))
        {
          std::function<void(SharedAudioMonitorFrame)> handler;
          {
            std::lock_guard lock(mutex_);
            handler = post_opus_diagnostic_handler_;
          }
          if (handler)
            handler(std::move(decoded));
        }
        else
        {
          post_opus_diagnostic_decode_failures_.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
    else if (diagnostic_decoder)
    {
      opus_decoder_destroy(diagnostic_decoder);
      diagnostic_decoder = nullptr;
    }

    broadcast_audio(*encoded);
  }
  if (diagnostic_decoder)
    opus_decoder_destroy(diagnostic_decoder);
}

std::uint64_t WebRtcHub::performance_counter() const noexcept
{
  if (realtime_audio_)
    return realtime_audio_->performance_counter();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

std::uint64_t WebRtcHub::performance_counter_us(std::uint64_t delta) const noexcept
{
  const auto frequency = realtime_audio_ ? realtime_audio_->performance_frequency() : 1'000'000'000;
  if (frequency == 0)
    return 0;
  return delta / frequency * 1'000'000ULL + delta % frequency * 1'000'000ULL / frequency;
}

void WebRtcHub::broadcast_audio(const EncodedOpusFrame& encoded)
{
  std::vector<std::shared_ptr<Peer>> peers;
  {
    std::lock_guard lock(mutex_);
    for (const auto& [_, peer] : peers_)
      peers.push_back(peer);
  }
  for (const auto& peer : peers)
  {
    if (!peer->audio_monitor || !peer->audio_monitor->isOpen())
      continue;
    const auto dropped = peer->encoded_audio_frames.push(encoded);
    peer->audio_sender_queue_drops.fetch_add(dropped, std::memory_order_relaxed);
    if (dropped != 0)
      audio_peer_push_failure_.store(true, std::memory_order_release);
    peer->audio_cv.notify_one();
  }
}

void WebRtcHub::audio_peer_sender_loop(Peer& peer)
{
  RecentLatencyWindow recent_latency;
  auto registration = realtime_audio_
                          ? realtime_audio_->register_current_thread(RealtimeAudioRole::rtp_sender)
                          : nullptr;
  if (registration && !registration->active())
  {
    peer.audio_mmcss_failures.fetch_add(1, std::memory_order_relaxed);
    audio_sender_mmcss_failures_.fetch_add(1, std::memory_order_relaxed);
  }
  while (!stopping_ && !peer.audio_stopping.load(std::memory_order_acquire))
  {
    {
      std::unique_lock lock(peer.audio_wait_mutex);
      peer.audio_cv.wait_for(lock,
                             std::chrono::milliseconds(100),
                             [&]
                             {
                               return stopping_ ||
                                      peer.audio_stopping.load(std::memory_order_acquire) ||
                                      peer.encoded_audio_frames.size() != 0;
                             });
    }
    if (stopping_ || peer.audio_stopping.load(std::memory_order_acquire))
      break;
    std::uint64_t expired = 0;
    auto encoded = peer.encoded_audio_frames.pop();
    if (!encoded)
      continue;
    const auto dequeued_at = performance_counter();
    if (dequeued_at > encoded->enqueue_send_ticks &&
        performance_counter_us(dequeued_at - encoded->enqueue_send_ticks) >
            encoded_audio_stale_after_us)
    {
      std::uint64_t newer_drops = 0;
      if (auto newest = peer.encoded_audio_frames.pop_newest(newer_drops))
      {
        ++expired;
        expired += newer_drops;
        encoded = std::move(newest);
      }
      if (dequeued_at > encoded->enqueue_send_ticks &&
          performance_counter_us(dequeued_at - encoded->enqueue_send_ticks) > 60'000)
      {
        ++expired;
        encoded.reset();
      }
    }
    if (expired != 0)
    {
      peer.audio_sender_expired_drops.fetch_add(expired, std::memory_order_relaxed);
      audio_sender_expired_drops_.fetch_add(expired, std::memory_order_relaxed);
    }
    if (!encoded)
      continue;

    const auto previous_timestamp = peer.audio_last_rtp_timestamp.load(std::memory_order_relaxed);
    const auto previous_topology =
        peer.audio_last_topology_generation.load(std::memory_order_relaxed);
    if (peer.audio_rtp_timeline_started.load(std::memory_order_relaxed) &&
        previous_topology == encoded->topology_generation)
    {
      const auto skipped = audio_rtp_sequence_skips(previous_timestamp, encoded->rtp_timestamp);
      if (skipped != 0 && peer.audio_monitor_packetizer)
      {
        // This sender thread is the sole writer of the packetizer sequence number. Advance it for
        // deliberately discarded audio so a standards-compliant receiver observes RTP loss rather
        // than an unexplained timestamp hole.
        auto& sequence = peer.audio_monitor_packetizer->rtpConfig->sequenceNumber;
        sequence = static_cast<std::uint16_t>(sequence + skipped);
        peer.audio_sender_sequence_skips.fetch_add(skipped, std::memory_order_relaxed);
      }
    }
    peer.audio_last_rtp_timestamp.store(encoded->rtp_timestamp, std::memory_order_relaxed);
    peer.audio_last_topology_generation.store(encoded->topology_generation,
                                              std::memory_order_relaxed);
    peer.audio_rtp_timeline_started.store(true, std::memory_order_relaxed);

    const auto send_start_ticks = performance_counter();
    const auto previous_send_start =
        peer.audio_last_send_start_ticks.exchange(send_start_ticks, std::memory_order_relaxed);
    if (previous_send_start != 0 && send_start_ticks > previous_send_start)
    {
      const auto interval_us = performance_counter_us(send_start_ticks - previous_send_start);
      peer.audio_last_departure_interval_us.store(interval_us, std::memory_order_relaxed);
      audio_departure_interval_.observe(interval_us);
      if (interval_us < 10'000)
        audio_departure_bursts_.fetch_add(1, std::memory_order_relaxed);
      if (interval_us > 30'000)
        audio_departure_gaps_.fetch_add(1, std::memory_order_relaxed);
    }
    const auto send_cycles_start = realtime_audio_ ? realtime_audio_->current_thread_cycles() : 0;
    bool send_exception = false;
    try
    {
      peer.audio_monitor->sendFrame(
          encoded->payload.data(), encoded->payload_bytes, rtc::FrameInfo(encoded->rtp_timestamp));
      peer.audio_monitor_packets.fetch_add(1, std::memory_order_relaxed);
      peer.audio_monitor_bytes.fetch_add(encoded->payload_bytes, std::memory_order_relaxed);
    }
    catch (const std::exception& error)
    {
      send_exception = true;
      peer.record_send_error(error.what());
      peer.audio_monitor_drops.fetch_add(1, std::memory_order_relaxed);
      peer.audio_send_exceptions.fetch_add(1, std::memory_order_relaxed);
      audio_send_exceptions_.fetch_add(1, std::memory_order_relaxed);
    }
    const auto send_cycles_end = realtime_audio_ ? realtime_audio_->current_thread_cycles() : 0;
    const auto send_end_ticks = performance_counter();
    if (send_cycles_end >= send_cycles_start)
    {
      const auto cycles = send_cycles_end - send_cycles_start;
      peer.audio_send_cycles.fetch_add(cycles, std::memory_order_relaxed);
      audio_send_cycles_.fetch_add(cycles, std::memory_order_relaxed);
    }
    const auto send_us = performance_counter_us(send_end_ticks - send_start_ticks);
    audio_send_latency_.observe(send_us);
    const auto send_p99 = recent_latency.observe(send_us);
    peer.audio_send_p95_us.store(recent_latency.percentile(95), std::memory_order_relaxed);
    peer.audio_send_p99_us.store(send_p99, std::memory_order_relaxed);

    const AudioPipelinePressure pressure{
        0, peer.encoded_audio_frames.size(), 0, send_p99, send_exception, false};
    const auto decision = peer.audio_overload_controller.update(pressure);
    peer.audio_overload_state.store(static_cast<std::uint8_t>(decision.state),
                                    std::memory_order_relaxed);
    peer.audio_profile_step.store(decision.profile.step, std::memory_order_relaxed);
    if (decision.state_changed && realtime_audio_)
      realtime_audio_->mark_audio_state(audio_overload_state_name(decision.state));

    audio_frame_telemetry_.publish({encoded->frame_index,
                                    encoded->topology_generation,
                                    encoded->capture_ticks,
                                    encoded->ipc_pop_ticks,
                                    encoded->encode_start_ticks,
                                    encoded->encode_end_ticks,
                                    encoded->enqueue_send_ticks,
                                    send_start_ticks,
                                    send_end_ticks,
                                    encoded->rtp_timestamp,
                                    encoded->payload_bytes,
                                    encoded->encoder_queue_occupancy,
                                    static_cast<std::uint8_t>(pressure.sender_queue_size),
                                    decision.state,
                                    encoded->input_gap,
                                    send_exception,
                                    encoded->dropped_oldest || expired != 0});
  }
}
} // namespace studio_remote
