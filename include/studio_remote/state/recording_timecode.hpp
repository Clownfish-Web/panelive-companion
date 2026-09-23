#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace studio_remote
{
inline std::string format_recording_timecode(std::uint64_t duration_ms)
{
  const auto milliseconds = duration_ms % 1'000;
  const auto total_seconds = duration_ms / 1'000;
  const auto seconds = total_seconds % 60;
  const auto total_minutes = total_seconds / 60;
  const auto minutes = total_minutes % 60;
  const auto hours = total_minutes / 60;
  char value[48]{};
  std::snprintf(value,
                sizeof(value),
                "%02llu:%02llu:%02llu.%03llu",
                static_cast<unsigned long long>(hours),
                static_cast<unsigned long long>(minutes),
                static_cast<unsigned long long>(seconds),
                static_cast<unsigned long long>(milliseconds));
  return value;
}

inline bool valid_recording_timecode(std::string_view value)
{
  if (value.size() < 12 || value.size() > 32)
    return false;
  const auto first_colon = value.find(':');
  const auto second_colon = value.find(':', first_colon + 1);
  const auto decimal = value.find('.', second_colon + 1);
  if (first_colon == std::string_view::npos || first_colon < 2 || second_colon != first_colon + 3 ||
      decimal != second_colon + 3 || decimal + 4 != value.size())
    return false;
  for (std::size_t index = 0; index < value.size(); ++index)
  {
    if (index == first_colon || index == second_colon || index == decimal)
      continue;
    if (std::isdigit(static_cast<unsigned char>(value[index])) == 0)
      return false;
  }
  const auto minutes = (value[first_colon + 1] - '0') * 10 + (value[first_colon + 2] - '0');
  const auto seconds = (value[second_colon + 1] - '0') * 10 + (value[second_colon + 2] - '0');
  return minutes < 60 && seconds < 60;
}
} // namespace studio_remote
