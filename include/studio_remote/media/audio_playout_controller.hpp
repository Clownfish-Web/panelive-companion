#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace studio_remote
{

struct AudioPlayoutPolicy
{
  std::size_t minimum_target_frames = 3;
  std::size_t initial_target_frames = 3;
  std::size_t maximum_target_frames = 8;
  std::size_t high_watermark_frames = 12;
  std::uint32_t stable_callbacks_before_decrease = 1'500;
  double maximum_rate_adjustment_ppm = 250.0;
};

// Owns bounded playout latency policy. Endpoint observations are required before queue depth may
// influence rate, so scheduler bursts alone cannot masquerade as clock drift.
class AudioPlayoutController
{
public:
  explicit AudioPlayoutController(AudioPlayoutPolicy policy = {});

  void reset() noexcept;
  bool ready(std::size_t queued_frames) const noexcept;
  bool above_high_watermark(std::size_t queued_frames) const noexcept;
  void report_underrun() noexcept;
  void report_stable_callback() noexcept;
  std::optional<double> observe_endpoint(std::uint64_t device_position,
                                         std::uint64_t correlated_qpc_100ns,
                                         std::uint64_t device_frequency,
                                         std::size_t queued_frames) noexcept;

  std::size_t target_frames() const noexcept;
  std::size_t low_watermark_frames() const noexcept;
  std::size_t high_watermark_frames() const noexcept;
  double rate_adjustment_ppm() const noexcept;
  std::uint64_t endpoint_observations() const noexcept;
  std::uint64_t rate_saturations() const noexcept;

private:
  AudioPlayoutPolicy policy_;
  std::size_t target_frames_ = 0;
  std::uint32_t stable_callbacks_ = 0;
  std::uint64_t previous_device_position_ = 0;
  std::uint64_t previous_qpc_100ns_ = 0;
  std::uint64_t endpoint_observations_ = 0;
  std::uint64_t rate_saturations_ = 0;
  double integral_error_ = 0.0;
  double rate_adjustment_ppm_ = 0.0;
};

} // namespace studio_remote
