#include "linux_thumbnail_mailbox.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace studio_remote::linux
{
LinuxThumbnailMailbox LinuxThumbnailMailbox::open(const std::string& name)
{
  LinuxThumbnailMailbox result(
      IpcMapping::open(name, sizeof(ipc::SourceThumbnailMapping), IpcAccess::read_write));
  if (!result.mapping_)
    throw std::runtime_error("OBS source thumbnail IPC is unavailable");
  return result;
}

LinuxThumbnailMailbox::LinuxThumbnailMailbox(IpcMapping memory)
    : memory_(std::move(memory)), mapping_(static_cast<ipc::SourceThumbnailMapping*>(memory_.get()))
{
}

LinuxThumbnailMailbox::LinuxThumbnailMailbox(LinuxThumbnailMailbox&& other) noexcept
    : memory_(std::move(other.memory_)), mapping_(std::exchange(other.mapping_, nullptr))
{
}

LinuxThumbnailMailbox::~LinuxThumbnailMailbox() = default;

ipc::SourceThumbnailMapping& LinuxThumbnailMailbox::mapping()
{
  if (!mapping_)
    throw std::logic_error("OBS thumbnail mapping is not owned");
  return *mapping_;
}
} // namespace studio_remote::linux
