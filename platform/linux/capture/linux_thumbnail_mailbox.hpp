#pragma once

#include <studio_remote/ipc/source_thumbnail_mapping.hpp>

#include "../ipc/linux_ipc_mapping.hpp"

namespace studio_remote::linux
{
/// A synchronous thumbnail call owns this writable mapping. The borrowed protocol view must not
/// outlive it. Partial opens unwind through RAII; no request or callback is owned by this resource.
class LinuxThumbnailMailbox final
{
public:
  static LinuxThumbnailMailbox open(const std::string& name);
  ~LinuxThumbnailMailbox();
  LinuxThumbnailMailbox(LinuxThumbnailMailbox&& other) noexcept;
  LinuxThumbnailMailbox(const LinuxThumbnailMailbox&) = delete;
  LinuxThumbnailMailbox& operator=(const LinuxThumbnailMailbox&) = delete;
  LinuxThumbnailMailbox& operator=(LinuxThumbnailMailbox&&) = delete;

  ipc::SourceThumbnailMapping& mapping();

private:
  explicit LinuxThumbnailMailbox(IpcMapping memory);
  IpcMapping memory_;
  ipc::SourceThumbnailMapping* mapping_ = nullptr;
};
} // namespace studio_remote::linux
