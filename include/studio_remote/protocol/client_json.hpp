#pragma once

#include <cstddef>
#include <string_view>
#include <studio_remote/protocol/bounded_json.hpp>

namespace studio_remote
{
inline constexpr std::size_t maximum_client_json_bytes = 1024 * 1024;
inline constexpr int maximum_client_json_depth = 32;
inline constexpr std::size_t maximum_client_json_nodes = 16'384;

/// Parse an untrusted client object with bounded tree construction. Duplicate decoded member
/// names are ambiguous and rejected. Errors never contain fragments of credentials or input.
inline nlohmann::json parse_client_json_object(std::string_view body)
{
  return parse_bounded_json_object(
      body, {maximum_client_json_bytes, maximum_client_json_depth, maximum_client_json_nodes});
}
} // namespace studio_remote
