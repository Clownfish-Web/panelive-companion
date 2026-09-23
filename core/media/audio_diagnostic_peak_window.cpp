#include <studio_remote/media/audio_diagnostic_peak_window.hpp>

#include <algorithm>
#include <cmath>

namespace studio_remote
{

void AudioDiagnosticPeakWindow::reset() noexcept
{
  first_ = 0;
  size_ = 0;
}

void AudioDiagnosticPeakWindow::discard_expired(std::uint64_t observed_ns) noexcept
{
  while (size_ != 0)
  {
    const auto& oldest = observations_[first_];
    if (oldest.observed_ns <= observed_ns &&
        observed_ns - oldest.observed_ns <= audio_diagnostic_peak_window_ns)
    {
      break;
    }
    first_ = (first_ + 1) % observations_.size();
    --size_;
  }
}

float AudioDiagnosticPeakWindow::observe(std::span<const float> samples,
                                         std::uint64_t observed_ns) noexcept
{
  if (size_ != 0)
  {
    const auto last = (first_ + size_ - 1) % observations_.size();
    if (observed_ns < observations_[last].observed_ns)
      reset();
  }
  discard_expired(observed_ns);

  float peak = 0.0F;
  for (const auto sample : samples)
    if (std::isfinite(sample))
      peak = std::max(peak, std::abs(sample));

  if (size_ == observations_.size())
  {
    first_ = (first_ + 1) % observations_.size();
    --size_;
  }
  const auto next = (first_ + size_) % observations_.size();
  observations_[next] = {observed_ns, peak};
  ++size_;
  return peak;
}

AudioDiagnosticPeakLevels AudioDiagnosticPeakWindow::levels(std::uint64_t observed_ns) noexcept
{
  discard_expired(observed_ns);
  AudioDiagnosticPeakLevels result;
  result.observations = size_;
  double sum = 0.0;
  for (std::size_t offset = 0; offset < size_; ++offset)
  {
    const auto peak = observations_[(first_ + offset) % observations_.size()].peak;
    result.maximum = std::max(result.maximum, peak);
    sum += peak;
  }
  if (size_ != 0)
    result.average = static_cast<float>(sum / static_cast<double>(size_));
  return result;
}

} // namespace studio_remote
