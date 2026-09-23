#include <studio_remote/credentials/device_name.hpp>
#include <studio_remote/credentials/pairing_manager.hpp>

#include <algorithm>

namespace studio_remote
{
namespace
{
constexpr std::string_view companion_service_id = "panelive-companion";
}

PairingExchangeError::PairingExchangeError(PairingExchangeFailure failure, std::string message)
    : std::runtime_error(std::move(message)), failure_(failure)
{
}

PairingExchangeFailure PairingExchangeError::failure() const noexcept
{
  return failure_;
}

PairingManager::PairingManager(const GatewayIdentity& identity,
                               AuditStore& store,
                               ICredentialStore& credentials,
                               std::vector<std::string> production_providers)
    : identity_(identity), store_(store), credentials_(credentials),
      production_providers_(std::move(production_providers))
{
}

json PairingManager::open(std::string gateway_url, Role role, std::chrono::seconds lifetime)
{
  return open_internal(std::move(gateway_url), role, false, lifetime);
}

json PairingManager::open_internal(std::string gateway_url,
                                   Role role,
                                   bool promote_first_provider_client,
                                   std::chrono::seconds lifetime)
{
  auto secret = credentials_.random_token(32);
  std::lock_guard lock(mutex_);
  secret_hash_ = credentials_.hash(secret);
  expires_ = Clock::now() + lifetime;
  role_ = role;
  promote_first_provider_client_ = promote_first_provider_client;
  const auto wall_expiry = std::chrono::system_clock::now() + lifetime;
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(wall_expiry.time_since_epoch()).count();
  return {{"gatewayUrl", std::move(gateway_url)},
          {"service", companion_service_id},
          {"instanceId", identity_.instance_id},
          {"certificateFingerprint", identity_.certificate_fingerprint},
          {"pairingSecret", secret},
          {"expiresAtUnixSeconds", seconds},
          {"role", role_name(role)},
          {"productionProviders", production_providers_}};
}

json PairingManager::open_default(std::string gateway_url,
                                  Role configured_role,
                                  std::chrono::seconds lifetime)
{
  return open_internal(
      std::move(gateway_url), configured_role, configured_role == Role::viewer, lifetime);
}

json PairingManager::exchange(std::string_view secret,
                              std::string_view device_name,
                              std::string_view production_provider,
                              DeviceInfo device_info)
{
  std::unique_lock lock(mutex_);
  if (secret_hash_.empty())
    throw PairingExchangeError(PairingExchangeFailure::pairing_not_open_or_already_consumed,
                               "Pairing is not open or the QR secret was already consumed");
  if (Clock::now() >= expires_)
    throw PairingExchangeError(PairingExchangeFailure::pairing_expired,
                               "Pairing QR secret expired");
  if (!credentials_.constant_time_equal(secret_hash_, credentials_.hash(secret)))
    throw PairingExchangeError(PairingExchangeFailure::pairing_secret_mismatch,
                               "Pairing QR secret mismatch");
  if (!valid_device_name(device_name))
    throw PairingExchangeError(PairingExchangeFailure::invalid_device_name, "Invalid device name");
  if (std::ranges::find(production_providers_, production_provider) == production_providers_.end())
    throw PairingExchangeError(PairingExchangeFailure::invalid_production_provider,
                               "Invalid or unavailable production provider");
  if (device_info.screen_size && !valid_device_screen_size(*device_info.screen_size))
    throw PairingExchangeError(PairingExchangeFailure::invalid_device_info,
                               "Invalid device screen size");
  device_info.type = effective_device_type(device_info);
  secret_hash_.clear(); // Single use, including if persistence below fails.
  auto token = credentials_.random_token(32);
  const auto assigned_role =
      promote_first_provider_client_ && !store_.has_active_operator(production_provider)
          ? Role::operator_
          : role_;
  Device device{credentials_.uuid_v4(),
                std::string(device_name),
                assigned_role,
                0,
                std::string(production_provider),
                std::move(device_info)};
  store_.add_device(device, credentials_.hash(token));
  json response = {{"deviceId", device.id},
                   {"deviceToken", token},
                   {"role", role_name(device.role)},
                   {"productionProvider", device.production_provider},
                   {"instanceId", identity_.instance_id},
                   {"certificateFingerprint", identity_.certificate_fingerprint}};
  auto handler = closed_handler_;
  lock.unlock();
  if (handler)
    handler();
  return response;
}

std::optional<Device> PairingManager::authenticate(std::string_view token)
{
  return authenticate_with_reason(token).device;
}

BearerAuthenticationResult PairingManager::authenticate_with_reason(std::string_view token)
{
  if (token.size() < 32 || token.size() > 256)
    return {{}, BearerAuthenticationFailure::malformed_token};
  const auto result = store_.authenticate_with_reason(credentials_.hash(token));
  if (result.device)
    return {result.device, BearerAuthenticationFailure::none};
  if (result.failure == DeviceAuthenticationFailure::device_revoked)
    return {{}, BearerAuthenticationFailure::device_revoked};
  return {{}, BearerAuthenticationFailure::token_not_found};
}

bool PairingManager::is_open() const
{
  std::lock_guard lock(mutex_);
  return !secret_hash_.empty() && Clock::now() < expires_;
}

void PairingManager::on_closed(std::function<void()> handler)
{
  std::lock_guard lock(mutex_);
  closed_handler_ = std::move(handler);
}
} // namespace studio_remote
