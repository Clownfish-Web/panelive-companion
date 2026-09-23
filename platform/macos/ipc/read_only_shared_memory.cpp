#include "read_only_shared_memory.hpp"

#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <utility>

namespace studio_remote::macos
{
ReadOnlySharedMemory::ReadOnlySharedMemory(int descriptor, std::size_t size)
    : descriptor_(descriptor), size_(size)
{
}
ReadOnlySharedMemory::~ReadOnlySharedMemory()
{
  if (address_ && munmap(address_, size_) != 0)
    syslog(LOG_ERR, "Could not release OBS read-only shared memory");
  if (descriptor_ >= 0 && ::close(descriptor_) != 0)
    syslog(LOG_ERR, "Could not close OBS shared-memory descriptor");
}
ReadOnlySharedMemory::ReadOnlySharedMemory(ReadOnlySharedMemory&& other) noexcept
    : descriptor_(std::exchange(other.descriptor_, -1)), size_(other.size_),
      address_(std::exchange(other.address_, nullptr))
{
}
std::span<const std::byte> ReadOnlySharedMemory::bytes() const noexcept
{
  return {static_cast<const std::byte*>(address_), address_ ? size_ : 0};
}
std::optional<ReadOnlySharedMemory> ReadOnlySharedMemory::open(std::string_view name,
                                                               std::size_t size)
{
  if (name.size() < 2 || name.size() > 30 || name.front() != '/' ||
      name.find('/', 1) != std::string_view::npos || name.find('\0') != std::string_view::npos ||
      size == 0 || size > static_cast<std::size_t>(std::numeric_limits<off_t>::max()))
    throw std::invalid_argument("OBS shared-memory name or size is invalid");
  const std::string terminated_name(name);
  // Darwin shm_open already sets FD_CLOEXEC and rejects O_CLOEXEC in its flags.
  const int descriptor = shm_open(terminated_name.c_str(), O_RDONLY, 0);
  if (descriptor < 0)
    return std::nullopt;
  ReadOnlySharedMemory result(descriptor, size);
  const int descriptor_flags = fcntl(descriptor, F_GETFD);
  if (descriptor_flags < 0 || (descriptor_flags & FD_CLOEXEC) == 0)
    return std::nullopt;
  struct stat status{};
  if (fstat(descriptor, &status) != 0 || status.st_uid != geteuid() ||
      (status.st_mode & 0077) != 0 || status.st_size < static_cast<off_t>(size))
    return std::nullopt;
  auto* address = mmap(nullptr, size, PROT_READ, MAP_SHARED, descriptor, 0);
  if (address == MAP_FAILED)
    return std::nullopt;
  result.address_ = address;
  return result;
}
} // namespace studio_remote::macos
