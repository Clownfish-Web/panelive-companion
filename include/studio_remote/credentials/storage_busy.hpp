#pragma once

#include <stdexcept>

namespace studio_remote
{
/// A bounded credential-store operation could not acquire its storage lock. Callers may retry
/// the same operation; this is neither an invalid credential nor malformed client input.
class CredentialStorageBusy final : public std::runtime_error
{
public:
  CredentialStorageBusy() : std::runtime_error("Credential storage is temporarily busy")
  {
  }
};
} // namespace studio_remote
