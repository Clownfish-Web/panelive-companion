#pragma once

#include <string_view>

namespace studio_remote
{
inline bool valid_device_name(std::string_view name)
{
  if (name.empty() || name.size() > 128)
    return false;

  bool has_non_space_character = false;
  for (const unsigned char character : name)
  {
    if (character < 0x20 || character == 0x7f)
      return false;
    if (character != ' ')
      has_non_space_character = true;
  }
  return has_non_space_character;
}
} // namespace studio_remote
