#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace studio_remote::obs_plugin::macos
{
/// Owns one producer mapping and its per-user file lease. The lease prevents a second producer
/// from resetting live IPC and is released by the kernel on process exit, including a crash.
/// Destroying the owner unlinks its mapping, but retains the lease file's stable inode.
class SharedMemoryOwner final
{
public:
  static std::unique_ptr<SharedMemoryOwner> create(std::string_view name, std::size_t size);
  ~SharedMemoryOwner();
  SharedMemoryOwner(const SharedMemoryOwner&) = delete;
  SharedMemoryOwner& operator=(const SharedMemoryOwner&) = delete;
  std::span<std::byte> bytes() const noexcept;

private:
  SharedMemoryOwner(std::string name, std::size_t size);
  bool acquire_lease();
  bool replace_stale_mapping();
  bool map_new_object();
  std::string name_;
  std::size_t size_;
  int lease_descriptor_ = -1;
  int memory_descriptor_ = -1;
  void* address_ = nullptr;
  bool owns_name_ = false;
};
} // namespace studio_remote::obs_plugin::macos
