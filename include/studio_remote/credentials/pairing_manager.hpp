#pragma once
#include <chrono>
#include <stdexcept>
#include <string>
#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/credentials/credential_store.hpp>
#include <vector>

namespace studio_remote
{
inline constexpr auto default_pairing_lifetime = std::chrono::minutes(5);

enum class PairingExchangeFailure
{
  none,
  pairing_not_open_or_already_consumed,
  pairing_expired,
  pairing_secret_mismatch,
  invalid_device_name,
  invalid_production_provider,
  invalid_device_info
};

class PairingExchangeError final : public std::runtime_error
{
public:
  PairingExchangeError(PairingExchangeFailure failure, std::string message);

  PairingExchangeFailure failure() const noexcept;

private:
  PairingExchangeFailure failure_ = PairingExchangeFailure::none;
};

enum class BearerAuthenticationFailure
{
  none,
  malformed_token,
  token_not_found,
  device_revoked
};

struct BearerAuthenticationResult
{
  std::optional<Device> device;
  BearerAuthenticationFailure failure = BearerAuthenticationFailure::none;
};

class PairingManager
{
public:
  PairingManager(const GatewayIdentity& identity,
                 AuditStore& store,
                 ICredentialStore& credentials,
                 std::vector<std::string> production_providers = {"obs", "vmix"});
  json open(std::string gateway_url,
            Role role,
            std::chrono::seconds lifetime = default_pairing_lifetime);
  json open_default(std::string gateway_url,
                    Role configured_role,
                    std::chrono::seconds lifetime = default_pairing_lifetime);
  json exchange(std::string_view secret,
                std::string_view device_name,
                std::string_view production_provider,
                DeviceInfo device_info = {});
  std::optional<Device> authenticate(std::string_view bearer_token);
  BearerAuthenticationResult authenticate_with_reason(std::string_view bearer_token);
  bool is_open() const;
  void on_closed(std::function<void()> handler);

private:
  json open_internal(std::string gateway_url,
                     Role role,
                     bool promote_first_provider_client,
                     std::chrono::seconds lifetime);

  const GatewayIdentity& identity_;
  AuditStore& store_;
  ICredentialStore& credentials_;
  mutable std::mutex mutex_;
  std::string secret_hash_;
  Clock::time_point expires_{};
  Role role_ = Role::viewer;
  bool promote_first_provider_client_ = false;
  std::vector<std::string> production_providers_;
  std::function<void()> closed_handler_;
};
} // namespace studio_remote
