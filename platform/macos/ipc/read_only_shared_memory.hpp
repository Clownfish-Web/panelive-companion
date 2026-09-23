#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace studio_remote::macos
{
/// Owns a read-only, owner-restricted POSIX mapping. The byte view borrows this object's lifetime.
/// Open never creates, truncates, changes permissions, or unlinks producer resources.
class ReadOnlySharedMemory final
{
public:
  static std::optional<ReadOnlySharedMemory> open(std::string_view name, std::size_t size);
  ~ReadOnlySharedMemory();
  ReadOnlySharedMemory(ReadOnlySharedMemory&& other) noexcept;
  ReadOnlySharedMemory(const ReadOnlySharedMemory&) = delete;
  ReadOnlySharedMemory& operator=(const ReadOnlySharedMemory&) = delete;
  ReadOnlySharedMemory& operator=(ReadOnlySharedMemory&&) = delete;
  std::span<const std::byte> bytes() const noexcept;

private:
  explicit ReadOnlySharedMemory(int descriptor, std::size_t size);
  int descriptor_;
  std::size_t size_;
  void* address_ = nullptr;
};
} // namespace studio_remote::macos
