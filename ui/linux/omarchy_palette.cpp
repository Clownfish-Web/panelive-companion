#include "omarchy_palette.hpp"

#include <gdk/gdk.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>

namespace studio_remote::linux
{
namespace
{
constexpr std::array color_tokens{"background",
                                  "foreground",
                                  "accent",
                                  "dark_background",
                                  "lighter_background",
                                  "selection",
                                  "muted",
                                  "dark_foreground",
                                  "red",
                                  "yellow",
                                  "green"};

std::string_view trim(std::string_view value)
{
  const auto begin = value.find_first_not_of(" \t\r");
  if (begin == std::string_view::npos)
    return {};
  return value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1);
}

bool hex_color(std::string_view value)
{
  return value.size() == 7 && value.front() == '#' &&
         std::ranges::all_of(value.substr(1),
                             [](char byte)
                             {
                               return (byte >= '0' && byte <= '9') ||
                                      (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
                             });
}

double luminance(const std::string& color)
{
  GdkRGBA rgba{};
  if (!gdk_rgba_parse(&rgba, color.c_str()))
    throw std::runtime_error("Invalid Omarchy color");
  const auto linear = [](double component)
  { return component <= 0.04045 ? component / 12.92 : std::pow((component + 0.055) / 1.055, 2.4); };
  return 0.2126 * linear(rgba.red) + 0.7152 * linear(rgba.green) + 0.0722 * linear(rgba.blue);
}

std::string readable(const std::string& foreground, const std::string& background)
{
  const auto front = luminance(foreground);
  const auto back = luminance(background);
  if ((std::max(front, back) + 0.05) / (std::min(front, back) + 0.05) >= 4.5)
    return foreground;
  return (back + 0.05) / 0.05 >= 1.05 / (back + 0.05) ? "#000000" : "#ffffff";
}
} // namespace

OmarchyPalette parse_omarchy_palette(std::string_view contents)
{
  if (contents.empty() || contents.size() > 16'384 || contents.find('\0') != contents.npos)
    throw std::runtime_error("Invalid Omarchy palette size or encoding");
  std::map<std::string, std::string> tokens;
  while (!contents.empty())
  {
    const auto end = contents.find('\n');
    const auto line = trim(contents.substr(0, end));
    contents = end == contents.npos ? std::string_view{} : contents.substr(end + 1);
    const auto equals = line.find('=');
    if (equals == line.npos || line.starts_with('#'))
      continue;
    const auto key = trim(line.substr(0, equals));
    if (key != "mode" && std::ranges::find(color_tokens, key) == color_tokens.end())
      continue;
    auto value = trim(line.substr(equals + 1));
    if (value.empty() || (value.front() != '"' && value.front() != '\''))
      throw std::runtime_error("Omarchy palette token must be a quoted value");
    const auto closing = value.find(value.front(), 1);
    if (closing == value.npos)
      throw std::runtime_error("Unterminated Omarchy palette token");
    const auto trailing = trim(value.substr(closing + 1));
    if (!trailing.empty() && !trailing.starts_with('#'))
      throw std::runtime_error("Unexpected data after Omarchy palette token");
    value = value.substr(1, closing - 1);
    if ((key == "mode" && value != "light" && value != "dark") ||
        (key != "mode" && !hex_color(value)))
      throw std::runtime_error("Invalid Omarchy palette token");
    if (!tokens.emplace(key, value).second)
      throw std::runtime_error("Duplicate Omarchy palette token");
  }
  for (const auto* required : color_tokens)
    if (!tokens.contains(required))
      throw std::runtime_error("Omarchy palette is incomplete");
  if (!tokens.contains("mode"))
    throw std::runtime_error("Omarchy palette is incomplete");
  return {tokens.at("background"),
          tokens.at("foreground"),
          tokens.at("accent"),
          tokens.at("mode") == "light",
          tokens.at("dark_background"),
          tokens.at("lighter_background"),
          tokens.at("selection"),
          tokens.at("muted"),
          tokens.at("dark_foreground"),
          tokens.at("red"),
          tokens.at("yellow"),
          tokens.at("green")};
}

std::string omarchy_palette_stylesheet(const OmarchyPalette& palette)
{
  for (const auto* color : {&palette.background,
                            &palette.foreground,
                            &palette.accent,
                            &palette.dark_background,
                            &palette.lighter_background,
                            &palette.selection,
                            &palette.muted,
                            &palette.dark_foreground,
                            &palette.red,
                            &palette.yellow,
                            &palette.green})
    if (!hex_color(*color))
      throw std::runtime_error("Invalid Omarchy color");
  const auto& foreground = palette.foreground;
  const auto& accent_text = palette.accent;
  const auto button_text = readable(palette.background, palette.accent);
  return "@define-color window_bg_color " + palette.background + ";\n" +
         "@define-color window_fg_color " + foreground + ";\n" + "@define-color view_bg_color " +
         palette.background + ";\n" + "@define-color view_fg_color " + foreground + ";\n" +
         "@define-color headerbar_bg_color " + palette.background + ";\n" +
         "@define-color headerbar_fg_color " + foreground + ";\n" +
         "@define-color accent_bg_color " + palette.accent + ";\n" +
         "@define-color accent_fg_color " + button_text + ";\n" + "@define-color accent_color " +
         accent_text + ";\n" + "@define-color card_bg_color " + palette.dark_background + ";\n" +
         "@define-color card_fg_color " + foreground + ";\n" + "@define-color sidebar_bg_color " +
         palette.dark_background + ";\n" + "@define-color sidebar_fg_color " + foreground + ";\n" +
         "@define-color popover_bg_color " + palette.dark_background + ";\n" +
         "@define-color popover_fg_color " + foreground + ";\n" + "@define-color dialog_bg_color " +
         palette.background + ";\n" + "@define-color dialog_fg_color " + foreground + ";\n" +
         "@define-color borders " + palette.muted + ";\n" + "@define-color panelive_control_bg " +
         palette.lighter_background + ";\n" + "@define-color panelive_muted_fg " +
         palette.dark_foreground + ";\n" + "@define-color panelive_selection_bg " +
         palette.selection + ";\n" + "@define-color panelive_selection_fg " +
         readable(foreground, palette.selection) + ";\n" + "@define-color success_color " +
         palette.green + ";\n" + "@define-color warning_color " + palette.yellow + ";\n" +
         "@define-color error_color " + palette.red + ";\n" + "@define-color destructive_color " +
         palette.red + ";\n" + "@define-color destructive_bg_color " + palette.red + ";\n" +
         "@define-color destructive_fg_color " + readable(foreground, palette.red) + ";\n" +
         "@define-color success_bg_color " + palette.green + ";\n" +
         "@define-color success_fg_color " + readable(foreground, palette.green) + ";\n" +
         "@define-color warning_bg_color " + palette.yellow + ";\n" +
         "@define-color warning_fg_color " + readable(foreground, palette.yellow) + ";\n" +
         "@define-color error_bg_color " + palette.red + ";\n" +
         "@define-color error_fg_color " + readable(foreground, palette.red) + ";\n" +
         "@define-color headerbar_backdrop_color " + palette.background + ";\n" +
         "@define-color headerbar_border_color " + palette.muted + ";\n" +
         "@define-color sidebar_backdrop_color " + palette.dark_background + ";\n" +
         "@define-color sidebar_border_color " + palette.muted + ";\n" +
         "@define-color secondary_sidebar_bg_color " + palette.dark_background + ";\n" +
         "@define-color secondary_sidebar_fg_color " + foreground + ";\n" +
         "@define-color secondary_sidebar_backdrop_color " + palette.dark_background + ";\n" +
         "@define-color secondary_sidebar_border_color " + palette.muted + ";\n" +
         "@define-color thumbnail_bg_color " + palette.lighter_background + ";\n" +
         "@define-color thumbnail_fg_color " + foreground + ";\n";
}
} // namespace studio_remote::linux
