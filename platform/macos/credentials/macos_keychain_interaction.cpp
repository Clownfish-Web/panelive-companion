#include "macos_keychain_interaction.hpp"

#include "macos_credential_store.hpp"

#include <Security/Security.h>
#include <syslog.h>

namespace studio_remote::macos
{
// File-based Keychain access does not reliably honor LAContext.interactionNotAllowed. Until
// that implementation honors SecItem's replacement API (Apple FB16959400), only these legacy
// process controls suppress its authorization UI. Keep this exception local to these calls.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
MacosKeychainInteraction::MacosKeychainInteraction(KeychainInteraction interaction)
{
  Boolean previous = false;
  auto status = SecKeychainGetUserInteractionAllowed(&previous);
  if (status != errSecSuccess)
    throw KeychainError(KeychainFailure::unexpected, status);
  status = SecKeychainSetUserInteractionAllowed(interaction == KeychainInteraction::allow);
  if (status != errSecSuccess)
    throw KeychainError(KeychainFailure::unexpected, status);
  previously_allowed_ = previous;
}

MacosKeychainInteraction::~MacosKeychainInteraction()
{
  if (SecKeychainSetUserInteractionAllowed(previously_allowed_) != errSecSuccess)
    syslog(LOG_ERR, "Could not restore the process Keychain interaction policy");
}
#pragma clang diagnostic pop
} // namespace studio_remote::macos
