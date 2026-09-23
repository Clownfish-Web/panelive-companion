#include "linux_obs_runtime_capabilities.hpp"

#include <studio_remote/common/types.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace studio_remote::linux
{
namespace
{
bool descriptor_is_owner_restricted(int descriptor)
{
  struct stat status
  {
  };
  return fstat(descriptor, &status) == 0 && status.st_uid == geteuid() &&
         (status.st_mode & 0077) == 0 &&
         status.st_size >= static_cast<off_t>(sizeof(ipc::ObsRuntimeCapabilitiesMapping));
}
} // namespace

LinuxObsRuntimeCapabilities::LinuxObsRuntimeCapabilities(std::string mapping_name)
    : mapping_name_(std::move(mapping_name))
{
}

LinuxObsRuntimeCapabilities::~LinuxObsRuntimeCapabilities()
{
  std::lock_guard lock(mutex_);
  close();
}

bool LinuxObsRuntimeCapabilities::recording_pause_available() const
{
  std::lock_guard lock(mutex_);
  if (!mapping_ && !open())
    return false;
  const auto snapshot = ipc::read_obs_runtime_capabilities(*mapping_, monotonic_ns());
  return snapshot && snapshot->recording_pause_available;
}

bool LinuxObsRuntimeCapabilities::open() const
{
  mapping_descriptor_ = shm_open(mapping_name_.c_str(), O_RDONLY | O_CLOEXEC, 0);
  if (mapping_descriptor_ < 0 || !descriptor_is_owner_restricted(mapping_descriptor_))
  {
    close();
    return false;
  }
  auto* address = mmap(nullptr,
                       sizeof(ipc::ObsRuntimeCapabilitiesMapping),
                       PROT_READ,
                       MAP_SHARED,
                       mapping_descriptor_,
                       0);
  if (address == MAP_FAILED)
  {
    close();
    return false;
  }
  mapping_ = static_cast<const ipc::ObsRuntimeCapabilitiesMapping*>(address);
  return true;
}

void LinuxObsRuntimeCapabilities::close() const
{
  if (mapping_)
    munmap(const_cast<ipc::ObsRuntimeCapabilitiesMapping*>(mapping_),
           sizeof(ipc::ObsRuntimeCapabilitiesMapping));
  if (mapping_descriptor_ >= 0)
    ::close(mapping_descriptor_);
  mapping_ = nullptr;
  mapping_descriptor_ = -1;
}

} // namespace studio_remote::linux
