#include "owner_security.hpp"

#include <aclapi.h>
#include <sddl.h>

#include <memory>
#include <string>

namespace studio_remote::obs_plugin::windows
{
namespace
{
struct HandleDeleter
{
  void operator()(void* value) const noexcept
  {
    if (!CloseHandle(value))
      OutputDebugStringW(L"OBS helper could not close its security token.\n");
  }
};
struct LocalDeleter
{
  void operator()(void* value) const noexcept
  {
    if (LocalFree(value))
      OutputDebugStringW(L"OBS helper could not release security information.\n");
  }
};
using LocalMemory = std::unique_ptr<void, LocalDeleter>;

std::vector<std::byte> current_user()
{
  HANDLE raw = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
    return {};
  std::unique_ptr<void, HandleDeleter> token(raw);
  DWORD size = 0;
  if (GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size) ||
      GetLastError() != ERROR_INSUFFICIENT_BUFFER || size < sizeof(TOKEN_USER))
    return {};
  std::vector<std::byte> information(size);
  if (!GetTokenInformation(token.get(), TokenUser, information.data(), size, &size))
    return {};
  return information;
}
} // namespace

OwnerSecurity::OwnerSecurity() : token_user_(current_user())
{
  if (token_user_.empty())
    return;
  const auto* user = reinterpret_cast<const TOKEN_USER*>(token_user_.data());
  wchar_t* converted = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &converted))
    return;
  const LocalMemory sid(converted);
  const std::wstring text(converted);
  const auto sddl = L"O:" + text + L"D:P(A;;GA;;;SY)(A;;GA;;;" + text + L")";
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
    descriptor_ = nullptr;
}

OwnerSecurity::~OwnerSecurity()
{
  if (descriptor_)
    LocalDeleter{}(descriptor_);
}

bool OwnerSecurity::valid() const noexcept
{
  return descriptor_ != nullptr;
}

SECURITY_ATTRIBUTES OwnerSecurity::attributes() const noexcept
{
  return {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
}

bool OwnerSecurity::validates(HANDLE object) const
{
  if (!valid())
    return false;
  PSID owner = nullptr;
  PACL acl = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const auto status = GetSecurityInfo(object,
                                      SE_KERNEL_OBJECT,
                                      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                      &owner,
                                      nullptr,
                                      &acl,
                                      nullptr,
                                      &raw);
  const LocalMemory information(raw);
  if (status != ERROR_SUCCESS || !owner || !acl)
    return false;
  const auto* user = reinterpret_cast<const TOKEN_USER*>(token_user_.data());
  SECURITY_DESCRIPTOR_CONTROL control{};
  DWORD revision = 0;
  if (!EqualSid(owner, user->User.Sid) || !GetSecurityDescriptorControl(raw, &control, &revision) ||
      (control & SE_DACL_PROTECTED) == 0)
    return false;
  for (DWORD index = 0; index < acl->AceCount; ++index)
  {
    void* raw_ace = nullptr;
    if (!GetAce(acl, index, &raw_ace))
      return false;
    const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
        ace->Header.AceSize < sizeof(ACCESS_ALLOWED_ACE))
      return false;
    auto* sid = const_cast<DWORD*>(&ace->SidStart);
    if (!IsValidSid(sid) ||
        (!EqualSid(sid, user->User.Sid) && !IsWellKnownSid(sid, WinLocalSystemSid)))
      return false;
  }
  return true;
}
} // namespace studio_remote::obs_plugin::windows
