#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace studio_remote
{
/// Validates Unicode scalar encoding without allocation; embedded U+0000 is valid UTF-8.
inline bool valid_utf8(std::string_view text) noexcept
{
  std::size_t position = 0;
  while (position < text.size())
  {
    const auto lead = static_cast<unsigned char>(text[position++]);
    if (lead < 0x80)
      continue;
    std::size_t remaining = 0;
    std::uint32_t scalar = 0;
    std::uint32_t minimum = 0;
    if (lead >= 0xc2 && lead <= 0xdf)
    {
      remaining = 1;
      scalar = lead & 0x1f;
      minimum = 0x80;
    }
    else if (lead >= 0xe0 && lead <= 0xef)
    {
      remaining = 2;
      scalar = lead & 0x0f;
      minimum = 0x800;
    }
    else if (lead >= 0xf0 && lead <= 0xf4)
    {
      remaining = 3;
      scalar = lead & 0x07;
      minimum = 0x10000;
    }
    else
      return false;
    if (text.size() - position < remaining)
      return false;
    for (std::size_t index = 0; index < remaining; ++index)
    {
      const auto next = static_cast<unsigned char>(text[position++]);
      if ((next & 0xc0) != 0x80)
        return false;
      scalar = (scalar << 6) | (next & 0x3f);
    }
    if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
      return false;
  }
  return true;
}
} // namespace studio_remote
