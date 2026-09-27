#pragma once

#include <Windows.h>

#include <memory>
#include <string_view>

namespace studio_remote::windows
{
struct IpcMappingHandleCloser
{
  void operator()(void* handle) const noexcept;
};
using IpcMappingHandle = std::unique_ptr<void, IpcMappingHandleCloser>;

struct IpcMappingViewCloser
{
  void operator()(void* view) const noexcept;
};
using IpcMappingView = std::unique_ptr<void, IpcMappingViewCloser>;

/// Opens a session-local helper mapping only when its owner is the current process user and
/// its protected DACL grants access solely to that user or LocalSystem. No view is mapped here.
/// Failure returns an empty owner; a security-policy rejection sets ERROR_ACCESS_DENIED.
IpcMappingHandle open_owner_restricted_mapping(DWORD access, std::wstring_view name);
} // namespace studio_remote::windows
