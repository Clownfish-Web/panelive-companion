#pragma once

#include <Windows.h>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

namespace studio_remote::obs_plugin::windows
{
class SharedMemoryOwner
{
public:
  static std::unique_ptr<SharedMemoryOwner> create(std::wstring_view name, std::size_t size);
  ~SharedMemoryOwner();
  SharedMemoryOwner(const SharedMemoryOwner&) = delete;
  SharedMemoryOwner& operator=(const SharedMemoryOwner&) = delete;
  std::span<std::byte> bytes() const noexcept;

private:
  SharedMemoryOwner() = default;
  // The named lease object's existence, not mutex thread ownership, excludes other producers.
  // Consumers never open it; process termination releases it even when consumers retain memory.
  HANDLE lease_ = nullptr;
  HANDLE mapping_ = nullptr;
  void* address_ = nullptr;
  std::size_t size_ = 0;
};
} // namespace studio_remote::obs_plugin::windows
