#pragma once

#include "macos_keychain_interaction.hpp"

#include <stdexcept>
#include <studio_remote/credentials/credential_store.hpp>

namespace studio_remote::macos
{
enum class KeychainFailure
{
  access_denied,
  unavailable,
  unexpected
};

/// Sanitized error category for management presentation; never contains an account or secret.
class KeychainError final : public std::runtime_error
{
public:
  KeychainError(KeychainFailure failure, int native_status);
  KeychainFailure failure() const noexcept;
  int native_status() const noexcept;

private:
  KeychainFailure failure_;
  int native_status_;
};

/// Per-instance, local Keychain storage. Call from an application worker: Security calls block.
/// The application must own MacosKeychainInteraction before workers start to suppress legacy
/// file-Keychain UI as well as per-query authentication. No iCloud sync or plaintext fallback.
/// Only explicit startup authorization uses a short-lived interactive instance, before listeners.
class MacosCredentialStore final : public ICredentialStore
{
public:
  explicit MacosCredentialStore(std::string instance_id,
                                KeychainInteraction interaction = KeychainInteraction::disallow);

  std::string random_token(std::size_t bytes) override;
  std::string hash(std::string_view value) override;
  bool constant_time_equal(std::string_view left, std::string_view right) override;
  std::string uuid_v4() override;
  void put(std::string_view key, std::string_view value) override;
  std::string get(std::string_view key) override;

private:
  const std::string instance_id_;
  const KeychainInteraction interaction_;
};
} // namespace studio_remote::macos
