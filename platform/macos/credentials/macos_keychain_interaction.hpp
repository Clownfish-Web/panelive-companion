#pragma once

namespace studio_remote::macos
{
enum class KeychainInteraction
{
  disallow,
  allow
};

/// The application creates one process-wide scope before starting credential workers and keeps
/// it until they join. This supplements LAContext for the file-based login Keychain, whose legacy
/// implementation can ignore SecItem's per-query UI controls. No Keychain data or ACL is changed.
/// An explicit interactive startup may nest an allow scope before gateway workers start.
class MacosKeychainInteraction final
{
public:
  explicit MacosKeychainInteraction(
      KeychainInteraction interaction = KeychainInteraction::disallow);
  ~MacosKeychainInteraction();
  MacosKeychainInteraction(const MacosKeychainInteraction&) = delete;
  MacosKeychainInteraction& operator=(const MacosKeychainInteraction&) = delete;

private:
  bool previously_allowed_ = false;
};
} // namespace studio_remote::macos
