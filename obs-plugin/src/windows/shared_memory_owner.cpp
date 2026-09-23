#include "shared_memory_owner.hpp"

#include "owner_security.hpp"

#include <cstdint>
#include <string>

namespace studio_remote::obs_plugin::windows
{
SharedMemoryOwner::~SharedMemoryOwner()
{
  if (address_ && !UnmapViewOfFile(address_))
    OutputDebugStringW(L"OBS helper could not unmap IPC.\n");
  if (mapping_ && !CloseHandle(mapping_))
    OutputDebugStringW(L"OBS helper could not close IPC.\n");
  if (lease_ && !CloseHandle(lease_))
    OutputDebugStringW(L"OBS helper could not release its IPC producer lease.\n");
}

std::span<std::byte> SharedMemoryOwner::bytes() const noexcept
{
  return {static_cast<std::byte*>(address_), address_ ? size_ : 0};
}

std::unique_ptr<SharedMemoryOwner> SharedMemoryOwner::create(std::wstring_view name,
                                                             std::size_t size)
{
  if (!name.starts_with(L"Local\\") || name.size() <= 6 || name.size() > 200 ||
      name.find(L'\\', 6) != std::wstring_view::npos ||
      name.find(L'\0') != std::wstring_view::npos || size == 0)
    return {};
  OwnerSecurity security;
  if (!security.valid())
    return {};
  auto attributes = security.attributes();
  auto owner = std::unique_ptr<SharedMemoryOwner>(new SharedMemoryOwner);
  const std::wstring mapping_name(name);
  const auto lease_name = mapping_name + L".producer";
  owner->lease_ = CreateMutexW(&attributes, FALSE, lease_name.c_str());
  if (!owner->lease_ || GetLastError() == ERROR_ALREADY_EXISTS)
    return {};
  const auto length = static_cast<std::uint64_t>(size);
  owner->mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                       &attributes,
                                       PAGE_READWRITE,
                                       static_cast<DWORD>(length >> 32U),
                                       static_cast<DWORD>(length),
                                       mapping_name.c_str());
  if (!owner->mapping_ || !security.validates(owner->mapping_))
    return {};
  // Windows retains the named object while a consumer retains its mapping handle. The publisher
  // responsibility for typed initialization, as before; this owner never resets consumer memory.
  owner->address_ = MapViewOfFile(owner->mapping_, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (!owner->address_)
    return {};
  owner->size_ = size;
  return owner;
}
} // namespace studio_remote::obs_plugin::windows
