#pragma once

#include <optional>
#include <string_view>
#include <studio_remote/credentials/pairing_manager.hpp>

namespace studio_remote::authentication_diagnostics
{

inline std::string_view pairing_failure_reason(PairingExchangeFailure failure)
{
  switch (failure)
  {
  case PairingExchangeFailure::none:
    return "none";
  case PairingExchangeFailure::pairing_not_open_or_already_consumed:
    return "pairing_not_open_or_already_consumed";
  case PairingExchangeFailure::pairing_expired:
    return "pairing_expired";
  case PairingExchangeFailure::pairing_secret_mismatch:
    return "pairing_secret_mismatch";
  case PairingExchangeFailure::invalid_device_name:
    return "invalid_device_name";
  case PairingExchangeFailure::invalid_production_provider:
    return "invalid_production_provider";
  case PairingExchangeFailure::invalid_device_info:
    return "invalid_device_info";
  }
  return "unknown_pairing_failure";
}

inline std::string_view bearer_failure_reason(BearerAuthenticationFailure failure)
{
  switch (failure)
  {
  case BearerAuthenticationFailure::none:
    return "none";
  case BearerAuthenticationFailure::malformed_token:
    return "malformed_bearer_token";
  case BearerAuthenticationFailure::token_not_found:
    return "unknown_device_token";
  case BearerAuthenticationFailure::device_revoked:
    return "device_revoked";
  }
  return "unknown_authentication_failure";
}

inline std::optional<std::string_view> tls_failure_reason(std::string_view message)
{
  if (message.find("sslize error") == std::string_view::npos)
    return std::nullopt;
  if (message.find("certificate unknown") != std::string_view::npos)
    return "certificate_unknown";
  if (message.find("unknown ca") != std::string_view::npos)
    return "unknown_certificate_authority";
  if (message.find("bad certificate") != std::string_view::npos)
    return "bad_certificate";
  if (message.find("unexpected eof") != std::string_view::npos)
    return "unexpected_eof";
  return "tls_handshake_failed";
}

} // namespace studio_remote::authentication_diagnostics
