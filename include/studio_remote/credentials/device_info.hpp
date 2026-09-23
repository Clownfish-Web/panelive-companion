#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace studio_remote
{
enum class DeviceType
{
  unknown,
  smartphone,
  tablet
};

inline constexpr std::uint32_t maximum_device_screen_logical_pixels = 32'768;
inline constexpr std::uint32_t tablet_minimum_short_side_logical_pixels = 600;

struct DeviceScreenSize
{
  std::uint32_t width_logical_pixels = 0;
  std::uint32_t height_logical_pixels = 0;

  bool operator==(const DeviceScreenSize&) const = default;
};

struct DeviceInfo
{
  DeviceType type = DeviceType::unknown;
  std::optional<DeviceScreenSize> screen_size;

  bool operator==(const DeviceInfo&) const = default;
};

inline constexpr std::string_view device_type_name(DeviceType type)
{
  switch (type)
  {
  case DeviceType::unknown:
    return "unknown";
  case DeviceType::smartphone:
    return "smartphone";
  case DeviceType::tablet:
    return "tablet";
  }
  return "unknown";
}

inline constexpr std::optional<DeviceType> parse_device_type(std::string_view value)
{
  if (value == "unknown")
    return DeviceType::unknown;
  if (value == "smartphone")
    return DeviceType::smartphone;
  if (value == "tablet")
    return DeviceType::tablet;
  return std::nullopt;
}

inline constexpr bool valid_device_screen_size(const DeviceScreenSize& size)
{
  return size.width_logical_pixels > 0 && size.height_logical_pixels > 0 &&
         size.width_logical_pixels <= maximum_device_screen_logical_pixels &&
         size.height_logical_pixels <= maximum_device_screen_logical_pixels;
}

inline constexpr DeviceType effective_device_type(const DeviceInfo& info)
{
  if (info.type != DeviceType::unknown)
    return info.type;
  if (!info.screen_size || !valid_device_screen_size(*info.screen_size))
    return DeviceType::unknown;
  const auto short_side =
      info.screen_size->width_logical_pixels < info.screen_size->height_logical_pixels
          ? info.screen_size->width_logical_pixels
          : info.screen_size->height_logical_pixels;
  return short_side >= tablet_minimum_short_side_logical_pixels ? DeviceType::tablet
                                                                : DeviceType::smartphone;
}
} // namespace studio_remote
