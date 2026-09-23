#include "libdatachannel_peer.hpp"
#include "libdatachannel_transport.hpp"

#include <studio_remote/ipc/audio_monitor_mapping.hpp>

#include <algorithm>
#include <array>
#include <opus/opus.h>

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

void WebRtcHub::audio_complexity_benchmark()
{
  constexpr std::array<int, 4> complexities{0, 3, 6, 10};
  constexpr std::size_t measurements = 100;
  std::array<float, ipc::audio_monitor_frame_samples * 2> signal{};
  for (std::size_t index = 0; index < signal.size(); ++index)
    signal[index] = static_cast<float>(static_cast<int>(index % 97) - 48) / 192.0F;
  int error = OPUS_OK;
  OpusEncoder* encoder =
      opus_encoder_create(ipc::audio_monitor_sample_rate, 2, OPUS_APPLICATION_AUDIO, &error);
  if (!encoder || error != OPUS_OK)
    return;
  std::array<unsigned char, encoded_opus_payload_capacity> payload{};
  int selected_complexity = 0;
  for (std::size_t profile = 0; profile < complexities.size() && !stopping_; ++profile)
  {
    if (opus_encoder_ctl(encoder, OPUS_RESET_STATE) != OPUS_OK ||
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(complexities[profile])) != OPUS_OK ||
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(128'000)) != OPUS_OK)
    {
      audio_opus_failures_.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    for (int warmup = 0; warmup < 10; ++warmup)
    {
      if (opus_encode_float(encoder,
                            signal.data(),
                            ipc::audio_monitor_frame_samples,
                            payload.data(),
                            static_cast<opus_int32>(payload.size())) <= 0)
      {
        audio_opus_failures_.fetch_add(1, std::memory_order_relaxed);
        break;
      }
    }
    std::array<std::uint64_t, measurements> durations{};
    std::array<std::uint64_t, measurements> cycles{};
    for (std::size_t sample = 0; sample < measurements && !stopping_; ++sample)
    {
      const auto start_ticks = performance_counter();
      const auto start_cycles = realtime_audio_ ? realtime_audio_->current_thread_cycles() : 0;
      const auto size = opus_encode_float(encoder,
                                          signal.data(),
                                          ipc::audio_monitor_frame_samples,
                                          payload.data(),
                                          static_cast<opus_int32>(payload.size()));
      const auto end_cycles = realtime_audio_ ? realtime_audio_->current_thread_cycles() : 0;
      const auto end_ticks = performance_counter();
      if (size <= 0)
      {
        audio_opus_failures_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      durations[sample] = performance_counter_us(end_ticks - start_ticks);
      cycles[sample] = end_cycles >= start_cycles ? end_cycles - start_cycles : 0;
    }
    if (stopping_)
      break;
    std::sort(durations.begin(), durations.end());
    std::sort(cycles.begin(), cycles.end());
    constexpr auto p99_index = (measurements - 1) * 99 / 100;
    audio_complexity_p99_us_[profile].store(durations[p99_index], std::memory_order_relaxed);
    audio_complexity_p99_cycles_[profile].store(cycles[p99_index], std::memory_order_relaxed);
    if (durations[p99_index] < 5'000)
      selected_complexity = complexities[profile];
  }
  opus_encoder_destroy(encoder);
  if (!stopping_)
  {
    audio_normal_complexity_.store(selected_complexity, std::memory_order_release);
    audio_benchmark_complete_.store(true, std::memory_order_release);
  }
}

void WebRtcHub::audio_encode_loop()
{
  OpusEncoder* encoder = nullptr;
  std::uint64_t last_sequence = 0;
  std::uint32_t last_topology_generation = 0;
  std::uint32_t last_timestamp = 0;
  std::uint64_t frame_index = 0;
  RecentLatencyWindow recent_latency;
  AudioEncoderProfile applied_profile{6, 128'000, OPUS_BANDWIDTH_FULLBAND, 2, 1};
  auto registration =
      realtime_audio_ ? realtime_audio_->register_current_thread(RealtimeAudioRole::opus_encoder)
                      : nullptr;
  if (registration && !registration->active())
    audio_encoder_mmcss_failures_.fetch_add(1, std::memory_order_relaxed);
  while (!stopping_)
  {
    SharedAudioMonitorFrame frame;
    std::size_t encoder_queue_size = 0;
    {
      std::unique_lock lock(mutex_);
      frame_cv_.wait_for(lock,
                         std::chrono::milliseconds(100),
                         [&] { return stopping_ || !audio_frames_.empty(); });
      if (stopping_)
        break;
      frame = audio_frames_.pop();
      encoder_queue_size = audio_frames_.size();
      bool requested = post_opus_diagnostic_enabled_.load(std::memory_order_acquire);
      for (const auto& [_, peer] : peers_)
        requested = requested || (peer->audio_monitor && peer->audio_monitor->isOpen());
      if (!requested)
      {
        audio_frames_.clear();
        frame.reset();
      }
    }
    if (!frame || frame->sample_rate != ipc::audio_monitor_sample_rate || frame->channels != 2 ||
        frame->samples.size() !=
            static_cast<std::size_t>(ipc::audio_monitor_frame_samples) * frame->channels)
      continue;

    if (!encoder)
    {
      int error = OPUS_OK;
      encoder = opus_encoder_create(static_cast<opus_int32>(frame->sample_rate),
                                    static_cast<int>(frame->channels),
                                    OPUS_APPLICATION_AUDIO,
                                    &error);
      if (!encoder || error != OPUS_OK)
      {
        encoder = nullptr;
        audio_encoder_failed_ = true;
        audio_opus_failures_.fetch_add(1, std::memory_order_relaxed);
        continue;
      }
      const auto ctl_ok = [&](int result)
      {
        if (result == OPUS_OK)
          return true;
        audio_opus_failures_.fetch_add(1, std::memory_order_relaxed);
        return false;
      };
      opus_int32 lookahead = 0;
      const bool configured =
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_BITRATE(128'000))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_VBR(1))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(1))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(6))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_AUTO))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_FORCE_CHANNELS(2))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_DTX(0))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(0))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(0))) &&
          ctl_ok(opus_encoder_ctl(encoder, OPUS_GET_LOOKAHEAD(&lookahead)));
      if (!configured)
      {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
        audio_encoder_failed_ = true;
        continue;
      }
      audio_opus_lookahead_.store(static_cast<std::uint32_t>(lookahead), std::memory_order_relaxed);
    }

    EncodedOpusFrame encoded;
    encoded.frame_index = ++frame_index;
    encoded.source_sequence = frame->sequence;
    encoded.first_sample_index = frame->first_sample_index;
    encoded.topology_generation = frame->topology_generation;
    encoded.rtp_timestamp = static_cast<std::uint32_t>(frame->first_sample_index);
    encoded.flags = frame->flags;
    encoded.ipc_pop_ticks = performance_counter();
    const auto frequency = realtime_audio_ ? realtime_audio_->performance_frequency() : 0;
    if (frequency != 0)
    {
      encoded.capture_ticks = frame->captured_ns / 1'000'000'000ULL * frequency +
                              frame->captured_ns % 1'000'000'000ULL * frequency / 1'000'000'000ULL;
    }
    encoded.encode_start_ticks = performance_counter();
    const auto encode_cycles_start = realtime_audio_ ? realtime_audio_->current_thread_cycles() : 0;
    const auto encoded_size =
        opus_encode_float(encoder,
                          frame->samples.data(),
                          ipc::audio_monitor_frame_samples,
                          reinterpret_cast<unsigned char*>(encoded.payload.data()),
                          static_cast<opus_int32>(encoded.payload.size()));
    encoded.encode_end_ticks = performance_counter();
    const auto encode_cycles_end = realtime_audio_ ? realtime_audio_->current_thread_cycles() : 0;
    encoded.encode_cycles =
        encode_cycles_end >= encode_cycles_start ? encode_cycles_end - encode_cycles_start : 0;
    audio_encode_cycles_.fetch_add(encoded.encode_cycles, std::memory_order_relaxed);
    if (encoded_size <= 0)
    {
      audio_encoder_failed_ = true;
      audio_opus_failures_.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    encoded.payload_bytes = static_cast<std::uint16_t>(encoded_size);
    audio_encoder_failed_ = false;
    const auto timestamp = encoded.rtp_timestamp;
    const bool topology_changed =
        last_sequence != 0 && frame->topology_generation != last_topology_generation;
    bool input_gap = false;
    if (last_sequence != 0)
    {
      if (frame->sequence > last_sequence + 1)
      {
        input_gap = true;
        audio_input_sequence_gaps_.fetch_add(frame->sequence - last_sequence - 1,
                                             std::memory_order_relaxed);
      }
      if (frame->sequence == last_sequence + 1 &&
          frame->topology_generation == last_topology_generation &&
          timestamp != last_timestamp + ipc::audio_monitor_frame_samples)
      {
        input_gap = true;
        audio_timestamp_discontinuities_.fetch_add(1, std::memory_order_relaxed);
      }
    }
    last_sequence = frame->sequence;
    last_topology_generation = frame->topology_generation;
    last_timestamp = timestamp;
    const auto encode_us =
        performance_counter_us(encoded.encode_end_ticks - encoded.encode_start_ticks);
    audio_encode_latency_.observe(encode_us);
    const auto encode_p99 = recent_latency.observe(encode_us);
    audio_encode_p95_us_.store(recent_latency.percentile(95), std::memory_order_relaxed);
    audio_encode_p99_us_.store(encode_p99, std::memory_order_relaxed);

    const auto sender_queue_size = encoded_audio_frames_.size();
    std::size_t peer_sender_queue_size = 0;
    std::uint64_t peer_send_p99 = 0;
    {
      std::lock_guard lock(mutex_);
      for (const auto& [_, peer] : peers_)
      {
        peer_sender_queue_size =
            std::max(peer_sender_queue_size, peer->encoded_audio_frames.size());
        peer_send_p99 =
            std::max(peer_send_p99, peer->audio_send_p99_us.load(std::memory_order_relaxed));
      }
    }
    audio_send_p99_us_.store(peer_send_p99, std::memory_order_relaxed);
    const AudioPipelinePressure pressure{
        encoder_queue_size,
        std::max(std::min(sender_queue_size + 1, encoded_audio_queue_capacity),
                 peer_sender_queue_size),
        encode_p99,
        peer_send_p99,
        sender_queue_size == encoded_audio_queue_capacity ||
            audio_peer_push_failure_.exchange(false, std::memory_order_acq_rel),
        topology_changed || audio_recovery_requested_.exchange(false, std::memory_order_acq_rel)};
    const auto decision = audio_overload_controller_.update(pressure);
    encoded.encoder_queue_occupancy = static_cast<std::uint8_t>(encoder_queue_size);
    encoded.sender_queue_occupancy = static_cast<std::uint8_t>(pressure.sender_queue_size);
    encoded.overload_state = decision.state;
    encoded.input_gap = input_gap;
    encoded.enqueue_send_ticks = performance_counter();
    const auto queue_drops = encoded_audio_frames_.push(std::move(encoded));
    audio_sender_queue_drops_.fetch_add(queue_drops, std::memory_order_relaxed);
    audio_overload_state_.store(static_cast<std::uint8_t>(decision.state),
                                std::memory_order_relaxed);
    audio_profile_step_.store(decision.profile.step, std::memory_order_relaxed);
    if (decision.state_changed)
    {
      audio_overload_transitions_.fetch_add(1, std::memory_order_relaxed);
      if (realtime_audio_)
        realtime_audio_->mark_audio_state(audio_overload_state_name(decision.state));
    }
    auto desired_profile = decision.profile;
    if (desired_profile.step == 0)
      desired_profile.complexity = audio_normal_complexity_.load(std::memory_order_acquire);
    if ((decision.profile_changed || desired_profile != applied_profile) &&
        desired_profile != applied_profile)
    {
      const bool changed =
          opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(desired_profile.complexity)) == OPUS_OK &&
          opus_encoder_ctl(encoder, OPUS_SET_BITRATE(desired_profile.bitrate)) == OPUS_OK &&
          opus_encoder_ctl(encoder, OPUS_SET_MAX_BANDWIDTH(desired_profile.maximum_bandwidth)) ==
              OPUS_OK &&
          opus_encoder_ctl(encoder, OPUS_SET_FORCE_CHANNELS(desired_profile.forced_channels)) ==
              OPUS_OK;
      if (changed)
        applied_profile = desired_profile;
      else
      {
        audio_opus_failures_.fetch_add(1, std::memory_order_relaxed);
        audio_encoder_failed_ = true;
      }
    }
    if (frame_index == 50 && applied_profile.step == 1 && encode_p99 < 3'000)
    {
      auto normal = AudioEncoderProfile{};
      normal.complexity = audio_normal_complexity_.load(std::memory_order_acquire);
      if (opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(normal.complexity)) == OPUS_OK)
        applied_profile = normal;
      else
        audio_opus_failures_.fetch_add(1, std::memory_order_relaxed);
    }
    audio_encoded_packets_.fetch_add(1, std::memory_order_relaxed);
    frame_cv_.notify_all();
  }
  if (encoder)
    opus_encoder_destroy(encoder);
}
} // namespace studio_remote
