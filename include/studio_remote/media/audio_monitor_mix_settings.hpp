#pragma once

#include <array>
#include <cstdint>

namespace studio_remote
{

inline constexpr std::array<std::uint32_t, 4> audio_monitor_headroom_presets_db{1, 2, 3, 4};
inline constexpr std::uint32_t default_audio_monitor_headroom_db = 2;

struct AudioMonitorMixSettings
{
  bool automatic_headroom = true;
  std::uint32_t attenuation_per_additional_source_db = default_audio_monitor_headroom_db;
  bool activate_audio_source_audition_bus = false;
};

inline bool valid_audio_monitor_headroom_db(std::uint32_t attenuation_db) noexcept
{
  for (const auto preset : audio_monitor_headroom_presets_db)
    if (preset == attenuation_db)
      return true;
  return false;
}

} // namespace studio_remote
