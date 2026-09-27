#include "http_authentication.hpp"

#include "authentication_diagnostics.hpp"
#include "http_server.hpp"

#include <cstddef>
#include <string>

namespace studio_remote
{
namespace
{
struct HeaderField
{
  std::string_view value;
  std::size_t count = 0;
};

HeaderField read_header(const mg_connection* connection, const char* name)
{
  HeaderField field;
  const auto* request = mg_get_request_info(connection);
  if (!request)
    return field;
  for (int index = 0; index < request->num_headers; ++index)
  {
    const auto& header = request->http_headers[index];
    if (mg_strcasecmp(header.name, name) == 0)
    {
      ++field.count;
      field.value = header.value;
    }
  }
  return field;
}

struct CredentialHeaders
{
  std::string_view token;
  std::string_view provider;
  std::string_view failure;
};

CredentialHeaders read_credentials(const mg_connection* connection)
{
  const auto authorization = read_header(connection, "Authorization");
  if (authorization.count == 0)
    return {{}, {}, "missing_authorization_header"};
  constexpr std::string_view prefix = "Bearer ";
  if (authorization.count != 1 || !authorization.value.starts_with(prefix))
    return {{}, {}, "malformed_bearer_authorization"};
  const auto provider = read_header(connection, "X-Production-Provider");
  if (provider.count > 1)
    return {{}, {}, "malformed_production_provider"};
  return {authorization.value.substr(prefix.size()), provider.value, {}};
}

std::string_view authentication_required_action(std::string_view reason)
{
  if (reason == "missing_production_provider" || reason == "production_provider_mismatch" ||
      reason == "malformed_production_provider")
    return "select_paired_production_provider";
  const bool stored_credential_rejected = reason == "malformed_bearer_token" ||
                                          reason == "unknown_device_token" ||
                                          reason == "device_revoked";
  return stored_credential_rejected ? "repair_pairing" : "provide_credentials";
}
} // namespace

json authentication_failure_response(std::string_view reason)
{
  return {{"error", "unauthorized"},
          {"reason", reason},
          {"retryable", false},
          {"requiredAction", authentication_required_action(reason)}};
}

GatewayHttpServer::AuthorizationResult
GatewayHttpServer::authorize(const mg_connection* connection, std::string_view transport) const
{
  const auto reject = [&](std::string_view reason) -> AuthorizationResult
  {
    if (endurance_log_ && endurance_log_->enabled())
      endurance_log_->record(
          "authentication",
          "bearerRejected",
          {{"transport", std::string(transport)},
           {"route", uri(connection)},
           {"reason", std::string(reason)},
           {"terminal", true},
           {"retryable", false},
           {"requiredAction", std::string(authentication_required_action(reason))}});
    return {{}, std::string(reason)};
  };
  // Borrowed views remain within this synchronous native callback. Reject duplicates even
  // when equal, before authentication can update the selected device's activity timestamp.
  const auto headers = read_credentials(connection);
  if (!headers.failure.empty())
    return reject(headers.failure);
  const auto result = pairing_.authenticate_with_reason(headers.token);
  if (!result.device)
    return reject(authentication_diagnostics::bearer_failure_reason(result.failure));
  if (headers.provider.empty())
    return reject("missing_production_provider");
  if (result.device->production_provider != headers.provider)
    return reject("production_provider_mismatch");
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record("authentication",
                           "bearerAccepted",
                           {{"transport", std::string(transport)},
                            {"route", uri(connection)},
                            {"deviceId", result.device->id},
                            {"role", role_name(result.device->role)},
                            {"productionProvider", result.device->production_provider}});
  return {result.device, {}};
}

void GatewayHttpServer::reply_authentication_failure(mg_connection* connection,
                                                     std::string_view reason)
{
  const auto admission = permit_public(connection, RequestScope::authentication);
  if (admission.retry_after_ms)
    reply_rate_limited(connection, "authentication", admission.retry_after_ms);
  else
    reply(connection, 401, authentication_failure_response(reason));
}
} // namespace studio_remote
