#pragma once

#include <string>
#include <string_view>

namespace studio_remote::linux
{
// Validated current Omarchy color tokens. No arbitrary theme text is admitted into GTK CSS.
struct OmarchyPalette
{
  std::string background;
  std::string foreground;
  std::string accent;
  bool light = false;
  std::string dark_background;
  std::string lighter_background;
  std::string selection;
  std::string muted;
  std::string dark_foreground;
  std::string red;
  std::string yellow;
  std::string green;
};

OmarchyPalette parse_omarchy_palette(std::string_view contents);
std::string omarchy_palette_stylesheet(const OmarchyPalette& palette);
} // namespace studio_remote::linux
