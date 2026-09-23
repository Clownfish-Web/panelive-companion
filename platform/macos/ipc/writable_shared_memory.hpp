#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace studio_remote::macos
{
/// Owns a writable, owner-restricted POSIX mapping. The byte view borrows this object's lifetime.
/// Open never creates, truncates, changes permissions, or unlinks producer resources.
class WritableSharedMemory final
{
public:
  static std::optional<WritableSharedMemory> open(std::string_view name, std::size_t size);
  ~WritableSharedMemory();
  WritableSharedMemory(WritableSharedMemory&& other) noexcept;
  WritableSharedMemory(const WritableSharedMemory&) = delete;
  WritableSharedMemory& operator=(const WritableSharedMemory&) = delete;
  WritableSharedMemory& operator=(WritableSharedMemory&&) = delete;
  std::span<std::byte> bytes() noexcept;

private:
  explicit WritableSharedMemory(int descriptor, std::size_t size);
  int descriptor_;
  std::size_t size_;
  void* address_ = nullptr;
};
} // namespace studio_remote::macos
