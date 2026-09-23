#include "pairing_exchange_protocol.hpp"

#include <cstdint>
#include <utility>

namespace studio_remote
{
namespace
{
std::optional<std::uint32_t> logical_dimension(const json& device_info, const char* field)
{
  const auto value = device_info.find(field);
  if (value == device_info.end() || !value->is_number_integer())
    return std::nullopt;
  if (value->is_number_unsigned())
  {
    const auto dimension = value->get<std::uint64_t>();
    if (dimension == 0 || dimension > maximum_device_screen_logical_pixels)
      return std::nullopt;
    return static_cast<std::uint32_t>(dimension);
  }
  const auto dimension = value->get<std::int64_t>();
  if (dimension <= 0 || dimension > maximum_device_screen_logical_pixels)
    return std::nullopt;
  return static_cast<std::uint32_t>(dimension);
}

bool optional_string(const json& body, const char* field, std::string& destination)
{
  const auto value = body.find(field);
  if (value == body.end())
    return true;
  if (!value->is_string())
    return false;
  destination = value->get<std::string>();
  return true;
}
} // namespace

PairingExchangeRequestResult parse_pairing_exchange_request(const json& body)
{
  if (!body.is_object())
    return {{}, PairingExchangeRequestFailure::invalid_request};

  PairingExchangeRequest request;
  if (!optional_string(body, "pairingSecret", request.pairing_secret) ||
      !optional_string(body, "deviceName", request.device_name) ||
      !optional_string(body, "productionProvider", request.production_provider))
    return {{}, PairingExchangeRequestFailure::invalid_request};

  const auto device_info = body.find("deviceInfo");
  if (device_info == body.end())
    return {std::move(request), PairingExchangeRequestFailure::none};
  if (!device_info->is_object())
    return {{}, PairingExchangeRequestFailure::invalid_device_info};

  const auto type = device_info->find("type");
  if (type != device_info->end())
  {
    if (!type->is_string())
      return {{}, PairingExchangeRequestFailure::invalid_device_info};
    const auto parsed = parse_device_type(type->get<std::string>());
    if (!parsed)
      return {{}, PairingExchangeRequestFailure::invalid_device_info};
    request.device_info.type = *parsed;
  }

  const bool has_width = device_info->contains("screenWidthLogicalPixels");
  const bool has_height = device_info->contains("screenHeightLogicalPixels");
  if (has_width != has_height)
    return {{}, PairingExchangeRequestFailure::invalid_device_info};
  if (has_width)
  {
    const auto width = logical_dimension(*device_info, "screenWidthLogicalPixels");
    const auto height = logical_dimension(*device_info, "screenHeightLogicalPixels");
    if (!width || !height)
      return {{}, PairingExchangeRequestFailure::invalid_device_info};
    request.device_info.screen_size = DeviceScreenSize{*width, *height};
  }
  request.device_info.type = effective_device_type(request.device_info);
  return {std::move(request), PairingExchangeRequestFailure::none};
}
} // namespace studio_remote
