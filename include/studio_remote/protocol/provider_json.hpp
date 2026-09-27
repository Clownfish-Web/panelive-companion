#pragma once

#include <studio_remote/protocol/bounded_json.hpp>

namespace studio_remote
{
inline constexpr int maximum_provider_json_depth = 64;
inline constexpr std::size_t maximum_provider_json_nodes = 262'144;

/// OBS/Streamlabs retain their transport-specific byte caps and share structural admission.
/// Protocol callers translate invalid_argument into their fixed, provider-specific errors.
inline nlohmann::json parse_provider_json_object(std::string_view body, std::size_t maximum_bytes)
{
  return parse_bounded_json_object(
      body, {maximum_bytes, maximum_provider_json_depth, maximum_provider_json_nodes});
}
} // namespace studio_remote
