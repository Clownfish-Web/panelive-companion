#pragma once

#include <cstddef>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{
struct JsonObjectLimits
{
  std::size_t maximum_bytes;
  int maximum_depth;
  std::size_t maximum_nodes;
};

/// Bound tree construction before copying/serializing untrusted JSON. Keys count as nodes and
/// duplicate decoded names are rejected per object. Errors contain no wire fragments.
inline nlohmann::json parse_bounded_json_object(std::string_view body, JsonObjectLimits limits)
{
  using json = nlohmann::json;
  if (body.size() > limits.maximum_bytes || body.find('\0') != std::string_view::npos)
    throw std::invalid_argument("invalid-json");
  std::size_t nodes = 0;
  std::vector<std::set<std::string>> object_keys;
  const auto callback = [&](int depth, json::parse_event_t event, json& value)
  {
    const bool container =
        event == json::parse_event_t::object_start || event == json::parse_event_t::array_start;
    if (container && depth >= limits.maximum_depth)
      throw std::invalid_argument("invalid-json");
    if (container || event == json::parse_event_t::value || event == json::parse_event_t::key)
      if (++nodes > limits.maximum_nodes)
        throw std::invalid_argument("invalid-json");
    if (event == json::parse_event_t::object_start)
      object_keys.emplace_back();
    else if (event == json::parse_event_t::object_end)
      object_keys.pop_back();
    else if (event == json::parse_event_t::key)
      if (!object_keys.back().insert(value.get_ref<const std::string&>()).second)
        throw std::invalid_argument("invalid-json");
    // Returning false would discard a value and continue parsing the hostile input.
    // Throw above to stop construction at the first exceeded budget or duplicate key.
    return true;
  };
  auto value = json::parse(body, callback, false);
  if (!value.is_object())
    throw std::invalid_argument("invalid-json");
  return value;
}
} // namespace studio_remote
