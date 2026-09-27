#include "windows_ipc_mapping.hpp"

#include <aclapi.h>

#include <cstddef>
#include <string>
#include <vector>

namespace studio_remote::windows
{
namespace
{
using SecurityDescriptor = std::unique_ptr<void, decltype(&LocalFree)>;

std::vector<BYTE> current_user()
{
  HANDLE raw = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
    return {};
  const IpcMappingHandle token(raw);
  DWORD length = 0;
  if (GetTokenInformation(token.get(), TokenUser, nullptr, 0, &length) ||
      GetLastError() != ERROR_INSUFFICIENT_BUFFER || length < sizeof(TOKEN_USER))
    return {};
  std::vector<BYTE> result(length);
  if (!GetTokenInformation(token.get(), TokenUser, result.data(), length, &length))
    return {};
  return result;
}

bool allowed_principal(const ACE_HEADER& header, PSID user)
{
  constexpr auto sid_offset = offsetof(ACCESS_ALLOWED_ACE, SidStart);
  if (header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
      header.AceSize < sid_offset + offsetof(SID, SubAuthority))
    return false;
  const auto& ace = reinterpret_cast<const ACCESS_ALLOWED_ACE&>(header);
  auto* sid = reinterpret_cast<SID*>(const_cast<DWORD*>(&ace.SidStart));
  if (GetSidLengthRequired(sid->SubAuthorityCount) > header.AceSize - sid_offset ||
      !IsValidSid(sid))
    return false;
  return EqualSid(sid, user) || IsWellKnownSid(sid, WinLocalSystemSid);
}

bool restricted_mapping(HANDLE mapping)
{
  PSID owner = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR raw = nullptr;
  const auto result = GetSecurityInfo(mapping,
                                      SE_KERNEL_OBJECT,
                                      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                      &owner,
                                      nullptr,
                                      &dacl,
                                      nullptr,
                                      &raw);
  const SecurityDescriptor descriptor(raw, LocalFree);
  if (result != ERROR_SUCCESS || !descriptor || !owner || !dacl || !IsValidSid(owner) ||
      !IsValidAcl(dacl))
    return false;
  const auto token = current_user();
  if (token.empty())
    return false;
  const auto* user = reinterpret_cast<const TOKEN_USER*>(token.data());
  SECURITY_DESCRIPTOR_CONTROL control{};
  DWORD revision = 0;
  if (!EqualSid(owner, user->User.Sid) ||
      !GetSecurityDescriptorControl(descriptor.get(), &control, &revision) ||
      (control & SE_DACL_PROTECTED) == 0)
    return false;
  for (DWORD index = 0; index < dacl->AceCount; ++index)
  {
    void* raw_ace = nullptr;
    if (!GetAce(dacl, index, &raw_ace) ||
        !allowed_principal(*static_cast<const ACE_HEADER*>(raw_ace), user->User.Sid))
      return false;
  }
  return true;
}
} // namespace

void IpcMappingHandleCloser::operator()(void* handle) const noexcept
{
  if (!CloseHandle(handle))
    OutputDebugStringW(L"Could not close helper IPC handle\n");
}

void IpcMappingViewCloser::operator()(void* view) const noexcept
{
  if (!UnmapViewOfFile(view))
    OutputDebugStringW(L"Could not unmap helper IPC view\n");
}

IpcMappingHandle open_owner_restricted_mapping(DWORD access, std::wstring_view name)
{
  if (!name.starts_with(L"Local\\") || name.size() <= 6 || name.size() > 200 ||
      name.find(L'\\', 6) != name.npos || name.find(L'\0') != name.npos)
  {
    SetLastError(ERROR_INVALID_NAME);
    return {};
  }
  const std::wstring terminated(name);
  IpcMappingHandle mapping(OpenFileMappingW(access | READ_CONTROL, FALSE, terminated.c_str()));
  if (!mapping || restricted_mapping(mapping.get()))
    return mapping;
  mapping.reset();
  SetLastError(ERROR_ACCESS_DENIED);
  return {};
}
} // namespace studio_remote::windows
