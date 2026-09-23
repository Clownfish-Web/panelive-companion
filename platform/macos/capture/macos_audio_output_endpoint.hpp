#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace studio_remote::macos
{
struct AudioOutputFormat
{
  double sample_rate = 0;
  std::uint32_t channels = 0;
};

struct AudioEndpointClock
{
  std::uint64_t device_position = 0;
  std::uint64_t correlated_host_100ns = 0;
  std::uint64_t device_frequency = 0;
};

/// One playback worker owns all calls. The native callback only releases fixed buffer slots.
/// Destruction synchronously disposes the queue before releasing callback state.
class MacosAudioOutputEndpoint final
{
public:
  explicit MacosAudioOutputEndpoint(std::string_view device_uid);
  ~MacosAudioOutputEndpoint();
  MacosAudioOutputEndpoint(const MacosAudioOutputEndpoint&) = delete;
  MacosAudioOutputEndpoint& operator=(const MacosAudioOutputEndpoint&) = delete;

  /// Accepts interleaved 48 kHz stereo, at most one 20 ms frame. False means backpressure.
  bool submit(std::span<const float> samples);
  void start();
  void stop();
  void set_rate_adjustment(double parts_per_million);
  std::size_t pending_buffer_frames() const noexcept;
  AudioOutputFormat device_format() const;
  /// Missing/invalid device-clock data is unavailable, never inferred from submitted buffers.
  std::optional<AudioEndpointClock> clock() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
