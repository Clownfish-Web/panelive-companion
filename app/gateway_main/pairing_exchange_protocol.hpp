#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/credentials/device_info.hpp>

#include <optional>
#include <string>

namespace studio_remote
{
struct PairingExchangeRequest
{
  std::string pairing_secret;
  std::string device_name = "Mobile device";
  std::string production_provider;
  DeviceInfo device_info;
};

enum class PairingExchangeRequestFailure
{
  none,
  invalid_request,
  invalid_device_info
};

struct PairingExchangeRequestResult
{
  std::optional<PairingExchangeRequest> request;
  PairingExchangeRequestFailure failure = PairingExchangeRequestFailure::none;
};

PairingExchangeRequestResult parse_pairing_exchange_request(const json& body);
} // namespace studio_remote
