#pragma once

#include <algorithm>
#include <cmath>

namespace studio_remote
{

inline constexpr float minimum_audio_diagnostic_gain_db = -24.0F;
inline constexpr float maximum_audio_diagnostic_gain_db = 12.0F;

inline bool valid_audio_diagnostic_gain(float gain_db) noexcept
{
  return std::isfinite(gain_db) && gain_db >= minimum_audio_diagnostic_gain_db &&
         gain_db <= maximum_audio_diagnostic_gain_db;
}

inline float audio_diagnostic_gain_multiplier(float gain_db) noexcept
{
  return std::pow(10.0F, gain_db / 20.0F);
}

struct AudioDiagnosticGainedSample
{
  float value = 0.0F;
  bool clipped = false;
};

inline AudioDiagnosticGainedSample apply_audio_diagnostic_gain(float sample,
                                                               float multiplier) noexcept
{
  const auto gained = sample * multiplier;
  return {std::clamp(gained, -1.0F, 1.0F), std::abs(gained) > 1.0F};
}

} // namespace studio_remote
