#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>

namespace studio_remote::streamlabs
{
inline constexpr std::string_view minimum_desktop_version = "1.21.9";

// Accept release file versions with three or four numeric WORD components. The fourth
// component is the Windows file revision. Tested-version metadata is not a support ceiling.
inline bool supported_native_desktop_version(std::string_view version,
                                             const std::array<std::uint16_t, 4>& minimum)
{
  if (version.empty() || version.size() > 23)
    return false;
  std::array<std::uint16_t, 4> components{};
  for (std::size_t index = 0; index < components.size(); ++index)
  {
    const auto separator = version.find('.');
    const auto component = version.substr(0, separator);
    if (component.empty())
      return false;
    const auto [end, error] =
        std::from_chars(component.data(), component.data() + component.size(), components[index]);
    if (error != std::errc{} || end != component.data() + component.size())
      return false;
    if (separator == std::string_view::npos)
      return index >= 2 && components >= minimum;
    version.remove_prefix(separator + 1);
  }
  return false;
}
inline bool supported_desktop_version(std::string_view version)
{
  return supported_native_desktop_version(version, {1, 21, 9, 0});
}
} // namespace studio_remote::streamlabs
