#include <studio_remote/media/audio_level_telemetry.hpp>

#include <algorithm>
#include <cmath>

namespace studio_remote
{
namespace
{
constexpr double minimum_dbfs = -120.0;

double amplitude_to_dbfs(double amplitude) noexcept
{
  if (!std::isfinite(amplitude) || amplitude <= 0.0)
    return minimum_dbfs;
  return std::clamp(20.0 * std::log10(amplitude), minimum_dbfs, 0.0);
}
} // namespace

void AudioLevelTelemetry::observe(const AudioMonitorFrame& frame) noexcept
{
  if (frame.samples.empty())
    return;

  double sum_squares = 0.0;
  double peak = 0.0;
  for (const auto sample : frame.samples)
  {
    if (!std::isfinite(sample))
      continue;
    const auto magnitude = std::abs(static_cast<double>(sample));
    peak = std::max(peak, magnitude);
    sum_squares += magnitude * magnitude;
  }
  const auto rms = std::sqrt(sum_squares / static_cast<double>(frame.samples.size()));

  publication_.fetch_add(1, std::memory_order_acq_rel);
  source_sample_index_.store(frame.first_sample_index, std::memory_order_relaxed);
  topology_generation_.store(frame.topology_generation, std::memory_order_relaxed);
  rms_dbfs_.store(amplitude_to_dbfs(rms), std::memory_order_relaxed);
  peak_dbfs_.store(amplitude_to_dbfs(peak), std::memory_order_relaxed);
  sequence_.store(frame.sequence, std::memory_order_relaxed);
  publication_.fetch_add(1, std::memory_order_release);
}

std::optional<AudioLevelSample> AudioLevelTelemetry::latest() const noexcept
{
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    const auto before = publication_.load(std::memory_order_acquire);
    if (before == 0 || (before & 1U) != 0)
      continue;
    AudioLevelSample result;
    result.sequence = sequence_.load(std::memory_order_relaxed);
    result.source_sample_index = source_sample_index_.load(std::memory_order_relaxed);
    result.topology_generation = topology_generation_.load(std::memory_order_relaxed);
    result.rms_dbfs = rms_dbfs_.load(std::memory_order_relaxed);
    result.peak_dbfs = peak_dbfs_.load(std::memory_order_relaxed);
    const auto after = publication_.load(std::memory_order_acquire);
    if (before == after && (after & 1U) == 0)
      return result;
  }
  return std::nullopt;
}

} // namespace studio_remote
