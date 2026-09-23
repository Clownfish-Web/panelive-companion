#pragma once

#include <Windows.h>

#include <cstddef>
#include <vector>

namespace studio_remote::obs_plugin::windows
{
// Native IPC ACL acquisition is fail-closed. The SID buffer owns the token data referenced by
// validation; no process token or converted SID allocation escapes construction.
class OwnerSecurity
{
public:
  OwnerSecurity();
  ~OwnerSecurity();
  OwnerSecurity(const OwnerSecurity&) = delete;
  OwnerSecurity& operator=(const OwnerSecurity&) = delete;
  bool valid() const noexcept;
  SECURITY_ATTRIBUTES attributes() const noexcept;
  bool validates(HANDLE object) const;

private:
  std::vector<std::byte> token_user_;
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
};
} // namespace studio_remote::obs_plugin::windows
