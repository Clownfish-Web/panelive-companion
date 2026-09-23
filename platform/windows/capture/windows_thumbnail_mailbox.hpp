#pragma once

#include <studio_remote/ipc/source_thumbnail_mapping.hpp>

#include <Windows.h>

namespace studio_remote::windows
{
/// A synchronous thumbnail call owns this writable mapping. The borrowed protocol view must not
/// outlive it. Partial opens unwind through RAII; no request or callback is owned by this resource.
class WindowsThumbnailMailbox final
{
public:
  static WindowsThumbnailMailbox open();
  ~WindowsThumbnailMailbox();
  WindowsThumbnailMailbox(WindowsThumbnailMailbox&& other) noexcept;
  WindowsThumbnailMailbox(const WindowsThumbnailMailbox&) = delete;
  WindowsThumbnailMailbox& operator=(const WindowsThumbnailMailbox&) = delete;
  WindowsThumbnailMailbox& operator=(WindowsThumbnailMailbox&&) = delete;

  ipc::SourceThumbnailMapping& mapping();

private:
  WindowsThumbnailMailbox() = default;
  HANDLE handle_ = nullptr;
  ipc::SourceThumbnailMapping* mapping_ = nullptr;
};
} // namespace studio_remote::windows
