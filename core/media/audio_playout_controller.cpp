#include <studio_remote/media/audio_playout_controller.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace studio_remote
{

AudioPlayoutController::AudioPlayoutController(AudioPlayoutPolicy policy) : policy_(policy)
{
  if (policy_.minimum_target_frames == 0 ||
      policy_.minimum_target_frames > policy_.initial_target_frames ||
      policy_.initial_target_frames > policy_.maximum_target_frames ||
      policy_.maximum_target_frames >= policy_.high_watermark_frames ||
      policy_.stable_callbacks_before_decrease == 0 || policy_.maximum_rate_adjustment_ppm <= 0.0)
    throw std::invalid_argument("Invalid audio playout policy");
  reset();
}

void AudioPlayoutController::reset() noexcept
{
  target_frames_ = policy_.initial_target_frames;
  stable_callbacks_ = 0;
  previous_device_position_ = 0;
  previous_qpc_100ns_ = 0;
  endpoint_observations_ = 0;
  integral_error_ = 0.0;
  rate_adjustment_ppm_ = 0.0;
}

bool AudioPlayoutController::ready(std::size_t queued_frames) const noexcept
{
  return queued_frames >= target_frames_;
}

bool AudioPlayoutController::above_high_watermark(std::size_t queued_frames) const noexcept
{
  return queued_frames > policy_.high_watermark_frames;
}

void AudioPlayoutController::report_underrun() noexcept
{
  target_frames_ = std::min(target_frames_ + 1, policy_.maximum_target_frames);
  stable_callbacks_ = 0;
  integral_error_ = 0.0;
}

void AudioPlayoutController::report_stable_callback() noexcept
{
  if (target_frames_ == policy_.minimum_target_frames)
    return;
  if (++stable_callbacks_ >= policy_.stable_callbacks_before_decrease)
  {
    --target_frames_;
    stable_callbacks_ = 0;
  }
}

std::optional<double> AudioPlayoutController::observe_endpoint(std::uint64_t device_position,
                                                               std::uint64_t correlated_qpc_100ns,
                                                               std::uint64_t device_frequency,
                                                               std::size_t queued_frames) noexcept
{
  if (device_frequency == 0 || correlated_qpc_100ns == 0)
    return std::nullopt;
  if (previous_qpc_100ns_ == 0)
  {
    previous_device_position_ = device_position;
    previous_qpc_100ns_ = correlated_qpc_100ns;
    return std::nullopt;
  }
  if (device_position <= previous_device_position_ || correlated_qpc_100ns <= previous_qpc_100ns_)
    return std::nullopt;

  const auto device_delta = device_position - previous_device_position_;
  const auto qpc_delta = correlated_qpc_100ns - previous_qpc_100ns_;
  const double device_seconds = static_cast<double>(device_delta) / device_frequency;
  const double qpc_seconds = static_cast<double>(qpc_delta) / 10'000'000.0;
  if (qpc_seconds < 0.25)
    return std::nullopt;
  previous_device_position_ = device_position;
  previous_qpc_100ns_ = correlated_qpc_100ns;
  if (!std::isfinite(device_seconds) || std::abs(device_seconds / qpc_seconds - 1.0) > 0.02)
    return std::nullopt;

  ++endpoint_observations_;
  const double queue_error = static_cast<double>(queued_frames) - target_frames_;
  integral_error_ = std::clamp(integral_error_ + queue_error, -100.0, 100.0);
  const double requested_ppm = queue_error * 18.0 + integral_error_ * 0.35;
  const double bounded = std::clamp(
      requested_ppm, -policy_.maximum_rate_adjustment_ppm, policy_.maximum_rate_adjustment_ppm);
  if (bounded != requested_ppm)
    ++rate_saturations_;
  if (std::abs(bounded - rate_adjustment_ppm_) < 1.0)
    return std::nullopt;
  rate_adjustment_ppm_ = bounded;
  return 48'000.0 * (1.0 + rate_adjustment_ppm_ / 1'000'000.0);
}

std::size_t AudioPlayoutController::target_frames() const noexcept
{
  return target_frames_;
}

std::size_t AudioPlayoutController::low_watermark_frames() const noexcept
{
  return target_frames_ > 1 ? target_frames_ - 1 : 0;
}

std::size_t AudioPlayoutController::high_watermark_frames() const noexcept
{
  return policy_.high_watermark_frames;
}

double AudioPlayoutController::rate_adjustment_ppm() const noexcept
{
  return rate_adjustment_ppm_;
}

std::uint64_t AudioPlayoutController::endpoint_observations() const noexcept
{
  return endpoint_observations_;
}

std::uint64_t AudioPlayoutController::rate_saturations() const noexcept
{
  return rate_saturations_;
}

} // namespace studio_remote
