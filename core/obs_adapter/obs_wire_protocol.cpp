#include <studio_remote/obs/obs_wire_protocol.hpp>

#include <stdexcept>
#include <studio_remote/obs/obs_event_subscriptions.hpp>
#include <studio_remote/obs/obs_pending_requests.hpp>
#include <studio_remote/obs/obs_protocol.hpp>
#include <utility>

namespace studio_remote
{
namespace
{
using json = nlohmann::json;
json envelope(std::string_view message)
{
  if (message.size() > maximum_obs_message_bytes)
    throw std::runtime_error("OBS message exceeds the size limit");
  auto value = json::parse(message, nullptr, false);
  if (value.is_discarded() || !value.is_object() || !value.contains("op") ||
      !value["op"].is_number_unsigned() || value["op"] > 255 || !value.contains("d") ||
      !value["d"].is_object())
    throw std::runtime_error("OBS sent an invalid message envelope");
  return value;
}
std::string text(const json& value, std::string_view field, std::size_t limit)
{
  const auto found = value.find(std::string(field));
  if (found == value.end() || !found->is_string())
    throw std::runtime_error("OBS sent an invalid message field");
  auto result = found->get<std::string>();
  if (result.empty() || result.size() > limit || result.find('\0') != std::string::npos)
    throw std::runtime_error("OBS sent an invalid message field");
  return result;
}
json object_or_empty(const json& value, std::string_view field)
{
  const auto found = value.find(std::string(field));
  if (found == value.end())
    return json::object();
  if (!found->is_object())
    throw std::runtime_error("OBS sent an invalid message object");
  return *found;
}
} // namespace

ObsHello parse_obs_hello(std::string_view message, std::string_view password)
{
  const auto value = envelope(message);
  const auto& data = value["d"];
  if (value["op"] != 0 || !data.contains("rpcVersion") ||
      !data["rpcVersion"].is_number_unsigned() || data["rpcVersion"] < 1)
    throw std::runtime_error("OBS sent an invalid Hello");
  ObsHello result;
  result.version = text(data, "obsWebSocketVersion", 128);
  json identify = {{"rpcVersion", 1}, {"eventSubscriptions", gateway_obs_event_subscriptions}};
  if (const auto authentication = data.find("authentication"); authentication != data.end())
  {
    if (!authentication->is_object())
      throw std::runtime_error("OBS sent invalid authentication data");
    if (password.empty())
      throw std::runtime_error("OBS requires a password");
    identify["authentication"] = obs_websocket_authentication(
        password, text(*authentication, "salt", 1024), text(*authentication, "challenge", 1024));
  }
  result.identify_message = json{{"op", 1}, {"d", std::move(identify)}}.dump();
  return result;
}

void validate_obs_identified(std::string_view message)
{
  const auto value = envelope(message);
  const auto& data = value["d"];
  if (value["op"] != 2 || !data.contains("negotiatedRpcVersion") ||
      !data["negotiatedRpcVersion"].is_number_unsigned() || data["negotiatedRpcVersion"] != 1)
    throw std::runtime_error("OBS did not accept Identify");
}

ObsWireMessage parse_obs_message(std::string_view message, const ObsPendingRequests& pending)
{
  const auto value = envelope(message);
  const auto& data = value["d"];
  if (value["op"] == 5)
    return ObsWireEvent{text(data, "eventType", 256), object_or_empty(data, "eventData")};
  if (value["op"] != 7)
    return std::monostate{};
  const auto id = text(data, "requestId", 256);
  // Preserve Windows: a late/unknown reply cannot invalidate a current request.
  if (!pending.contains(id))
    return std::monostate{};
  const auto status = object_or_empty(data, "requestStatus");
  if (!status.contains("result") || !status["result"].is_boolean() || !status.contains("code") ||
      !status["code"].is_number_unsigned() || status["code"] > 65535)
    throw std::runtime_error("OBS sent an invalid request status");
  return ObsWireResponse{id,
                         status["result"].get<bool>(),
                         status["code"].get<std::uint16_t>(),
                         object_or_empty(data, "responseData")};
}

std::string make_obs_request(std::string_view type, std::string_view id, json data)
{
  if (type.empty() || type.size() > 256 || type.find('\0') != std::string_view::npos ||
      id.empty() || id.size() > 256 || id.find('\0') != std::string_view::npos ||
      (!data.is_null() && !data.is_object()))
    throw std::invalid_argument("OBS request is invalid");
  // Preserve Windows wire data, including null for a request without arguments.
  return json{{"op", 6},
              {"d", {{"requestType", type}, {"requestId", id}, {"requestData", std::move(data)}}}}
      .dump();
}
void complete_obs_response(ObsPendingRequests& pending, ObsWireResponse response)
{
  if (response.accepted)
    pending.resolve(response.id, std::move(response.data));
  else
    pending.reject(response.id,
                   std::make_exception_ptr(std::runtime_error(
                       "OBS request failed (code " + std::to_string(response.status_code) + ")")));
}
} // namespace studio_remote
