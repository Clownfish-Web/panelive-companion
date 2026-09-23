#include "shared_memory_owner.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace studio_remote::obs_plugin::macos
{
namespace
{
struct Descriptor
{
  explicit Descriptor(int descriptor) : value(descriptor)
  {
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int value;
  ~Descriptor()
  {
    if (value >= 0 && ::close(value) != 0)
      syslog(LOG_ERR, "OBS helper could not close an IPC descriptor");
  }
};
bool restricted_owner(int descriptor)
{
  struct stat status{};
  return fstat(descriptor, &status) == 0 && status.st_uid == geteuid() &&
         (status.st_mode & 0077) == 0;
}
} // namespace
SharedMemoryOwner::SharedMemoryOwner(std::string name, std::size_t size)
    : name_(std::move(name)), size_(size)
{
}
SharedMemoryOwner::~SharedMemoryOwner()
{
  if (address_ && munmap(address_, size_) != 0)
    syslog(LOG_ERR, "OBS helper could not unmap IPC");
  if (memory_descriptor_ >= 0 && ::close(memory_descriptor_) != 0)
    syslog(LOG_ERR, "OBS helper could not close IPC memory");
  if (owns_name_ && shm_unlink(name_.c_str()) != 0)
    syslog(LOG_ERR, "OBS helper could not unlink IPC memory");
  if (lease_descriptor_ >= 0 && ::close(lease_descriptor_) != 0)
    syslog(LOG_ERR, "OBS helper could not release its IPC lease");
}
std::span<std::byte> SharedMemoryOwner::bytes() const noexcept
{
  return {static_cast<std::byte*>(address_), address_ ? size_ : 0};
}
std::unique_ptr<SharedMemoryOwner> SharedMemoryOwner::create(std::string_view name,
                                                             std::size_t size)
{
  if (name.size() < 2 || name.size() > 30 || name.front() != '/' ||
      name.find('/', 1) != std::string_view::npos || name.find('\0') != std::string_view::npos ||
      size == 0 || size > static_cast<std::size_t>(std::numeric_limits<off_t>::max()))
    return {};
  auto owner = std::unique_ptr<SharedMemoryOwner>(new SharedMemoryOwner(std::string(name), size));
  if (!owner->acquire_lease() || !owner->replace_stale_mapping() || !owner->map_new_object())
    return {};
  return owner;
}
bool SharedMemoryOwner::acquire_lease()
{
  const auto length = confstr(_CS_DARWIN_USER_TEMP_DIR, nullptr, 0);
  if (length < 2 || length > 4096)
    return false;
  std::vector<char> buffer(length);
  if (confstr(_CS_DARWIN_USER_TEMP_DIR, buffer.data(), buffer.size()) != length)
    return false;
  std::string directory(buffer.data());
  while (directory.size() > 1 && directory.back() == '/')
    directory.pop_back();
  const Descriptor parent{
      ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
  if (parent.value < 0 || !restricted_owner(parent.value))
    return false;
  const auto filename = name_.substr(1) + ".lock";
  lease_descriptor_ =
      openat(parent.value, filename.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (lease_descriptor_ < 0 || !restricted_owner(lease_descriptor_))
    return false;
  struct stat status{};
  if (fstat(lease_descriptor_, &status) != 0 || !S_ISREG(status.st_mode) || status.st_nlink != 1)
    return false;
  return flock(lease_descriptor_, LOCK_EX | LOCK_NB) == 0;
}
bool SharedMemoryOwner::replace_stale_mapping()
{
  // A cooperating live producer holds the lease, so an existing owner-restricted object here
  // belongs to a previous crashed attempt. Replace it to give readers a fresh generation.
  const Descriptor existing{shm_open(name_.c_str(), O_RDONLY, 0)};
  if (existing.value < 0)
    return errno == ENOENT;
  if (!restricted_owner(existing.value))
    return false;
  return shm_unlink(name_.c_str()) == 0;
}
bool SharedMemoryOwner::map_new_object()
{
  // Darwin sets FD_CLOEXEC for shm_open and rejects O_CLOEXEC. Permissions must be right at
  // creation because fchmod is unsupported for these descriptors.
  memory_descriptor_ = shm_open(name_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
  if (memory_descriptor_ < 0)
    return false;
  owns_name_ = true;
  const int flags = fcntl(memory_descriptor_, F_GETFD);
  if (!restricted_owner(memory_descriptor_) || flags < 0 || (flags & FD_CLOEXEC) == 0 ||
      ftruncate(memory_descriptor_, static_cast<off_t>(size_)) != 0)
    return false;
  auto* mapping = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, memory_descriptor_, 0);
  if (mapping == MAP_FAILED)
    return false;
  address_ = mapping;
  return true;
}
} // namespace studio_remote::obs_plugin::macos
