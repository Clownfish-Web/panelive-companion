#include "linux_ipc_mapping.hpp"

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace studio_remote::linux
{
IpcMapping IpcMapping::open(const std::string& name, std::size_t size, IpcAccess access)
{
  IpcMapping result;
  if (name.empty() || name.front() != '/' || name.find('/', 1) != std::string::npos ||
      name.find('\0') != std::string::npos || size == 0)
    return result;
  const bool writable = access == IpcAccess::read_write;
  result.descriptor_ = shm_open(name.c_str(), (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC, 0);
  struct stat status
  {
  };
  if (result.descriptor_ < 0 || fstat(result.descriptor_, &status) != 0 ||
      !S_ISREG(status.st_mode) || status.st_uid != geteuid() || (status.st_mode & 0077) != 0 ||
      status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) < size)
    return result;
  auto* address = mmap(
      nullptr, size, PROT_READ | (writable ? PROT_WRITE : 0), MAP_SHARED, result.descriptor_, 0);
  if (address != MAP_FAILED)
  {
    result.address_ = address;
    result.size_ = size;
  }
  return result;
}
IpcMapping::IpcMapping(IpcMapping&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)),
      address_(std::exchange(other.address_, nullptr)), size_(std::exchange(other.size_, 0))
{
}
IpcMapping::~IpcMapping()
{
  if (address_ && munmap(address_, size_) != 0)
    std::fputs("Could not unmap OBS IPC\n", stderr);
  if (descriptor_ >= 0 && close(descriptor_) != 0)
    std::fputs("Could not close OBS IPC descriptor\n", stderr);
}
void* IpcMapping::get() const noexcept
{
  return address_;
}
} // namespace studio_remote::linux
