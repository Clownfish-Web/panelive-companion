#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace studio_remote
{

constexpr std::size_t encoded_opus_payload_capacity = 4'000;
constexpr std::size_t encoded_audio_queue_capacity = 12;
constexpr std::uint64_t encoded_audio_stale_after_us = 160'000;

// Returns the intentionally omitted 20 ms packets represented by consecutive 48 kHz RTP
// timestamps. Large or malformed discontinuities are left to media-session recovery.
std::uint16_t audio_rtp_sequence_skips(std::uint32_t previous_timestamp,
                                       std::uint32_t current_timestamp) noexcept;

enum class AudioOverloadState : std::uint8_t
{
  normal,
  degrade,
  shed,
  recover
};

std::string_view audio_overload_state_name(AudioOverloadState state) noexcept;

struct AudioEncoderProfile
{
  int complexity = 10;
  int bitrate = 128'000;
  int maximum_bandwidth = 1105;
  int forced_channels = 2;
  std::uint8_t step = 0;

  bool operator==(const AudioEncoderProfile&) const = default;
};

struct EncodedOpusFrame
{
  std::uint64_t frame_index = 0;
  std::uint64_t source_sequence = 0;
  std::uint64_t capture_ticks = 0;
  std::uint64_t ipc_pop_ticks = 0;
  std::uint64_t encode_start_ticks = 0;
  std::uint64_t encode_end_ticks = 0;
  std::uint64_t enqueue_send_ticks = 0;
  std::uint64_t encode_cycles = 0;
  std::uint64_t first_sample_index = 0;
  std::uint32_t topology_generation = 0;
  std::uint32_t rtp_timestamp = 0;
  std::uint16_t payload_bytes = 0;
  std::uint16_t flags = 0;
  std::uint8_t encoder_queue_occupancy = 0;
  std::uint8_t sender_queue_occupancy = 0;
  AudioOverloadState overload_state = AudioOverloadState::normal;
  bool input_gap = false;
  bool dropped_oldest = false;
  std::array<std::byte, encoded_opus_payload_capacity> payload{};
};

struct AudioFrameTimingRecord
{
  std::uint64_t frame_index = 0;
  std::uint32_t topology_generation = 0;
  std::uint64_t capture_qpc = 0;
  std::uint64_t ipc_pop_qpc = 0;
  std::uint64_t encode_start_qpc = 0;
  std::uint64_t encode_end_qpc = 0;
  std::uint64_t enqueue_send_qpc = 0;
  std::uint64_t send_start_qpc = 0;
  std::uint64_t send_end_qpc = 0;
  std::uint32_t rtp_timestamp = 0;
  std::uint16_t payload_bytes = 0;
  std::uint8_t encoder_queue_occupancy = 0;
  std::uint8_t sender_queue_occupancy = 0;
  AudioOverloadState overload_state = AudioOverloadState::normal;
  bool input_gap = false;
  bool send_exception = false;
  bool dropped_oldest = false;
};

class AudioFrameTelemetryRing
{
public:
  void publish(AudioFrameTimingRecord record) noexcept;
  std::optional<AudioFrameTimingRecord> latest() const noexcept;
  std::uint64_t published() const noexcept;

private:
  void lock() const noexcept;
  void unlock() const noexcept;

  mutable std::atomic_flag guard_ = ATOMIC_FLAG_INIT;
  std::array<AudioFrameTimingRecord, 256> records_{};
  std::uint64_t published_ = 0;
};

// Fixed-allocation single-producer/single-consumer queue. A short acquire/release spin guard makes
// oldest replacement safe when the producer overtakes the consumer. The consumer releases the
// guard before packetization or network work.
class BoundedEncodedAudioQueue
{
public:
  std::uint64_t push(EncodedOpusFrame frame) noexcept;
  std::optional<EncodedOpusFrame> pop() noexcept;
  std::optional<EncodedOpusFrame> pop_newest(std::uint64_t& dropped) noexcept;
  void clear() noexcept;
  std::size_t size() const noexcept;
  std::size_t high_watermark() const noexcept;

private:
  void lock() const noexcept;
  void unlock() const noexcept;

  mutable std::atomic_flag guard_ = ATOMIC_FLAG_INIT;
  std::array<EncodedOpusFrame, encoded_audio_queue_capacity> frames_{};
  std::size_t read_index_ = 0;
  std::size_t size_ = 0;
  std::size_t high_watermark_ = 0;
};

class FixedLatencyHistogram
{
public:
  void observe(std::uint64_t microseconds) noexcept;
  std::array<std::uint64_t, 9> buckets() const noexcept;
  std::uint64_t maximum_us() const noexcept;

private:
  std::array<std::atomic<std::uint64_t>, 9> buckets_{};
  std::atomic<std::uint64_t> maximum_us_{0};
};

struct AudioPipelinePressure
{
  std::size_t encoder_queue_size = 0;
  std::size_t sender_queue_size = 0;
  std::uint64_t encode_p99_us = 0;
  std::uint64_t send_p99_us = 0;
  bool sender_push_failed = false;
  bool recovery_requested = false;
};

struct AudioOverloadDecision
{
  AudioOverloadState state = AudioOverloadState::normal;
  AudioEncoderProfile profile;
  bool state_changed = false;
  bool profile_changed = false;
};

// Deterministic hysteresis policy. Call once for each 20 ms source tick.
class AudioOverloadController
{
public:
  AudioOverloadDecision update(const AudioPipelinePressure& pressure) noexcept;
  AudioOverloadState state() const noexcept;
  AudioEncoderProfile profile() const noexcept;

private:
  void set_step(std::uint8_t step) noexcept;

  AudioOverloadState state_ = AudioOverloadState::normal;
  AudioEncoderProfile profile_;
  std::uint16_t pressure_ticks_ = 0;
  std::uint16_t low_pressure_ticks_ = 0;
  std::uint16_t profile_ticks_ = 0;
};

} // namespace studio_remote
