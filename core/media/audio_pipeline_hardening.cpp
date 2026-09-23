#include <studio_remote/media/audio_pipeline_hardening.hpp>

#include <algorithm>
#include <thread>

namespace studio_remote
{
namespace
{
constexpr std::array<std::uint64_t, 8> latency_bounds_us{
    250, 500, 1'000, 2'000, 3'000, 5'000, 10'000, 20'000};
constexpr std::uint32_t audio_rtp_packet_samples = 960;
constexpr std::uint32_t maximum_recoverable_gap_packets = 50;

AudioEncoderProfile profile_for_step(std::uint8_t step) noexcept
{
  switch (step)
  {
  case 0:
    return {10, 128'000, 1105, 2, step};
  case 1:
    return {6, 128'000, 1105, 2, step};
  case 2:
    return {3, 128'000, 1105, 2, step};
  case 3:
    return {3, 96'000, 1105, 2, step};
  case 4:
    return {0, 96'000, 1105, 2, step};
  case 5:
    return {0, 64'000, 1104, 2, step};
  default:
    return {0, 64'000, 1104, 1, 6};
  }
}
} // namespace

std::uint16_t audio_rtp_sequence_skips(std::uint32_t previous_timestamp,
                                       std::uint32_t current_timestamp) noexcept
{
  const auto timestamp_delta = current_timestamp - previous_timestamp;
  if (timestamp_delta <= audio_rtp_packet_samples ||
      timestamp_delta % audio_rtp_packet_samples != 0)
    return 0;
  const auto packet_delta = timestamp_delta / audio_rtp_packet_samples;
  if (packet_delta > maximum_recoverable_gap_packets + 1)
    return 0;
  return static_cast<std::uint16_t>(packet_delta - 1);
}

std::string_view audio_overload_state_name(AudioOverloadState state) noexcept
{
  switch (state)
  {
  case AudioOverloadState::normal:
    return "NORMAL";
  case AudioOverloadState::degrade:
    return "DEGRADE";
  case AudioOverloadState::shed:
    return "SHED";
  case AudioOverloadState::recover:
    return "RECOVER";
  }
  return "RECOVER";
}

void BoundedEncodedAudioQueue::lock() const noexcept
{
  while (guard_.test_and_set(std::memory_order_acquire))
    std::this_thread::yield();
}

void BoundedEncodedAudioQueue::unlock() const noexcept
{
  guard_.clear(std::memory_order_release);
}

std::uint64_t BoundedEncodedAudioQueue::push(EncodedOpusFrame frame) noexcept
{
  lock();
  std::uint64_t dropped = 0;
  if (size_ == frames_.size())
  {
    read_index_ = (read_index_ + 1) % frames_.size();
    --size_;
    dropped = 1;
    frame.dropped_oldest = true;
  }
  const auto write_index = (read_index_ + size_) % frames_.size();
  frames_[write_index] = std::move(frame);
  ++size_;
  high_watermark_ = std::max(high_watermark_, size_);
  unlock();
  return dropped;
}

std::optional<EncodedOpusFrame> BoundedEncodedAudioQueue::pop() noexcept
{
  lock();
  if (size_ == 0)
  {
    unlock();
    return std::nullopt;
  }
  auto frame = std::move(frames_[read_index_]);
  read_index_ = (read_index_ + 1) % frames_.size();
  --size_;
  unlock();
  return frame;
}

std::optional<EncodedOpusFrame>
BoundedEncodedAudioQueue::pop_newest(std::uint64_t& dropped) noexcept
{
  lock();
  if (size_ == 0)
  {
    unlock();
    return std::nullopt;
  }
  dropped += size_ - 1;
  const auto newest = (read_index_ + size_ - 1) % frames_.size();
  auto frame = std::move(frames_[newest]);
  read_index_ = (newest + 1) % frames_.size();
  size_ = 0;
  unlock();
  return frame;
}

void BoundedEncodedAudioQueue::clear() noexcept
{
  lock();
  read_index_ = 0;
  size_ = 0;
  unlock();
}

std::size_t BoundedEncodedAudioQueue::size() const noexcept
{
  lock();
  const auto result = size_;
  unlock();
  return result;
}

std::size_t BoundedEncodedAudioQueue::high_watermark() const noexcept
{
  lock();
  const auto result = high_watermark_;
  unlock();
  return result;
}

void FixedLatencyHistogram::observe(std::uint64_t microseconds) noexcept
{
  const auto position =
      std::lower_bound(latency_bounds_us.begin(), latency_bounds_us.end(), microseconds);
  buckets_[static_cast<std::size_t>(position - latency_bounds_us.begin())].fetch_add(
      1, std::memory_order_relaxed);
  auto maximum = maximum_us_.load(std::memory_order_relaxed);
  while (microseconds > maximum &&
         !maximum_us_.compare_exchange_weak(maximum, microseconds, std::memory_order_relaxed))
  {
  }
}

std::array<std::uint64_t, 9> FixedLatencyHistogram::buckets() const noexcept
{
  std::array<std::uint64_t, 9> result{};
  for (std::size_t index = 0; index < result.size(); ++index)
    result[index] = buckets_[index].load(std::memory_order_relaxed);
  return result;
}

std::uint64_t FixedLatencyHistogram::maximum_us() const noexcept
{
  return maximum_us_.load(std::memory_order_relaxed);
}

void AudioFrameTelemetryRing::lock() const noexcept
{
  while (guard_.test_and_set(std::memory_order_acquire))
    std::this_thread::yield();
}

void AudioFrameTelemetryRing::unlock() const noexcept
{
  guard_.clear(std::memory_order_release);
}

void AudioFrameTelemetryRing::publish(AudioFrameTimingRecord record) noexcept
{
  lock();
  records_[published_ % records_.size()] = record;
  ++published_;
  unlock();
}

std::optional<AudioFrameTimingRecord> AudioFrameTelemetryRing::latest() const noexcept
{
  lock();
  if (published_ == 0)
  {
    unlock();
    return std::nullopt;
  }
  auto result = records_[(published_ - 1) % records_.size()];
  unlock();
  return result;
}

std::uint64_t AudioFrameTelemetryRing::published() const noexcept
{
  lock();
  const auto result = published_;
  unlock();
  return result;
}

void AudioOverloadController::set_step(std::uint8_t step) noexcept
{
  profile_ = profile_for_step(std::min<std::uint8_t>(step, 6));
  profile_ticks_ = 0;
}

AudioOverloadDecision
AudioOverloadController::update(const AudioPipelinePressure& pressure) noexcept
{
  const auto old_state = state_;
  const auto old_profile = profile_;
  if (pressure.recovery_requested)
  {
    state_ = AudioOverloadState::recover;
    set_step(1);
    pressure_ticks_ = 0;
    low_pressure_ticks_ = 0;
  }
  else
  {
    const bool elevated = pressure.encoder_queue_size >= 2 || pressure.sender_queue_size >= 6 ||
                          pressure.encode_p99_us > 5'000 || pressure.send_p99_us > 2'000;
    const bool severe = pressure.sender_queue_size >= 11 || pressure.sender_push_failed;
    const bool low = pressure.encoder_queue_size == 0 && pressure.sender_queue_size <= 4 &&
                     pressure.encode_p99_us < 3'000 && pressure.send_p99_us < 2'000;
    pressure_ticks_ = elevated ? static_cast<std::uint16_t>(pressure_ticks_ + 1) : 0;
    low_pressure_ticks_ = low ? static_cast<std::uint16_t>(low_pressure_ticks_ + 1) : 0;
    ++profile_ticks_;

    if (severe)
    {
      state_ = AudioOverloadState::shed;
      if (profile_.step < 4)
        set_step(4);
    }
    else if (state_ == AudioOverloadState::normal && pressure_ticks_ >= 3)
    {
      state_ = AudioOverloadState::degrade;
      set_step(1);
    }
    else if ((state_ == AudioOverloadState::degrade || state_ == AudioOverloadState::shed) &&
             elevated && profile_ticks_ >= 100 && profile_.step < 6)
    {
      set_step(static_cast<std::uint8_t>(profile_.step + 1));
      if (profile_.step >= 4)
        state_ = AudioOverloadState::shed;
    }
    else if (state_ == AudioOverloadState::shed && low_pressure_ticks_ >= 100)
    {
      state_ = AudioOverloadState::degrade;
      set_step(3);
      low_pressure_ticks_ = 0;
    }
    else if ((state_ == AudioOverloadState::degrade || state_ == AudioOverloadState::recover) &&
             low_pressure_ticks_ >= 500)
    {
      state_ = AudioOverloadState::normal;
      set_step(0);
      low_pressure_ticks_ = 0;
    }
  }
  return {state_, profile_, state_ != old_state, profile_ != old_profile};
}

AudioOverloadState AudioOverloadController::state() const noexcept
{
  return state_;
}

AudioEncoderProfile AudioOverloadController::profile() const noexcept
{
  return profile_;
}

} // namespace studio_remote
