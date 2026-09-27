#pragma once

#include <cstddef>
#include <string>

namespace studio_remote::linux
{
enum class IpcAccess
{
  read_only,
  read_write
};

// Owns one POSIX descriptor and its fixed-size view. Validates owner, private mode and extent
// on that same descriptor before mapping. Native adapters own the typed protocol interpretation.
class IpcMapping final
{
public:
  static IpcMapping open(const std::string& name, std::size_t size, IpcAccess access);
  ~IpcMapping();
  IpcMapping(IpcMapping&& other) noexcept;
  IpcMapping(const IpcMapping&) = delete;
  IpcMapping& operator=(const IpcMapping&) = delete;
  IpcMapping& operator=(IpcMapping&&) = delete;
  void* get() const noexcept;

private:
  IpcMapping() = default;
  int descriptor_ = -1;
  void* address_ = nullptr;
  std::size_t size_ = 0;
};
} // namespace studio_remote::linux
