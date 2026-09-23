#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include <nlohmann/json.hpp>

namespace studio_remote
{
class ObsPendingRequests;
inline constexpr std::size_t maximum_obs_message_bytes = 16 * 1024 * 1024;

struct ObsHello
{
  std::string version;
  std::string identify_message;
};
struct ObsWireEvent
{
  std::string type;
  nlohmann::json data;
};
struct ObsWireResponse
{
  std::string id;
  bool accepted = false;
  std::uint16_t status_code = 0;
  nlohmann::json data;
};
using ObsWireMessage = std::variant<std::monostate, ObsWireEvent, ObsWireResponse>;

// These functions are the JSON boundary. Parse failures have fixed text, never wire payloads.
ObsHello parse_obs_hello(std::string_view message, std::string_view password);
void validate_obs_identified(std::string_view message);
// Unknown opcodes and replies for IDs absent from the table return monostate. Status parsing
// follows the ID lookup, matching Windows dispatch. No callbacks run during parsing/completion.
ObsWireMessage parse_obs_message(std::string_view message, const ObsPendingRequests& pending);
std::string make_obs_request(std::string_view type, std::string_view id, nlohmann::json data);
void complete_obs_response(ObsPendingRequests& pending, ObsWireResponse response);
} // namespace studio_remote
