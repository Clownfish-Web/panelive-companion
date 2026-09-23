#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace studio_remote
{

inline constexpr std::uint64_t audio_diagnostic_peak_window_ns = 1'000'000'000ULL;
inline constexpr std::size_t audio_diagnostic_peak_window_capacity = 128;

struct AudioDiagnosticPeakLevels
{
  float maximum = 0.0F;
  float average = 0.0F;
  std::size_t observations = 0;
};

// Retains bounded per-frame peaks for the selected diagnostic tap. Callers provide a monotonic
// observation time so the trailing window can be tested without wall-clock sleeps.
class AudioDiagnosticPeakWindow
{
public:
  void reset() noexcept;
  float observe(std::span<const float> samples, std::uint64_t observed_ns) noexcept;
  AudioDiagnosticPeakLevels levels(std::uint64_t observed_ns) noexcept;

private:
  struct Observation
  {
    std::uint64_t observed_ns = 0;
    float peak = 0.0F;
  };

  void discard_expired(std::uint64_t observed_ns) noexcept;

  std::array<Observation, audio_diagnostic_peak_window_capacity> observations_{};
  std::size_t first_ = 0;
  std::size_t size_ = 0;
};

} // namespace studio_remote
