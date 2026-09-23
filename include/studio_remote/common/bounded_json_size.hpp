#pragma once

#include <studio_remote/common/types.hpp>

#include <cstddef>
#include <optional>

namespace studio_remote
{
/// Conservative budget for retained JSON nodes/strings and worst-case escaped serialization.
/// Stops traversing once the budget/depth is exceeded; never serializes oversized producer input.
inline std::optional<std::size_t>
bounded_json_size(const json& value, std::size_t budget, std::size_t depth = 0)
{
  constexpr std::size_t node_cost = 128;
  if (depth > 16 || budget < node_cost)
    return std::nullopt;
  auto used = node_cost;
  const auto add_text = [&](const std::string& text)
  {
    // A moved string can retain a large allocation after resize/clear.
    if (text.capacity() > (budget - used) / 6)
      return false;
    used += text.capacity() * 6;
    return true;
  };
  if (value.is_string())
  {
    if (!add_text(value.get_ref<const std::string&>()))
      return std::nullopt;
  }
  else if (value.is_structured())
  {
    if (value.is_array())
    {
      const auto& array = value.get_ref<const json::array_t&>();
      const auto spare = array.capacity() - array.size();
      if (spare > (budget - used) / sizeof(json))
        return std::nullopt;
      used += spare * sizeof(json);
    }
    for (auto entry = value.begin(); entry != value.end(); ++entry)
    {
      if (value.is_object() && !add_text(entry.key()))
        return std::nullopt;
      const auto size = bounded_json_size(entry.value(), budget - used, depth + 1);
      if (!size)
        return std::nullopt;
      used += *size;
    }
  }
  else if (value.is_binary() || value.is_discarded())
    return std::nullopt;
  return used;
}
} // namespace studio_remote
