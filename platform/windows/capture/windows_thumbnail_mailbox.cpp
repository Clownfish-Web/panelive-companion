#include "windows_thumbnail_mailbox.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace studio_remote::windows
{
WindowsThumbnailMailbox WindowsThumbnailMailbox::open()
{
  WindowsThumbnailMailbox result;
  constexpr DWORD access = FILE_MAP_READ | FILE_MAP_WRITE;
  result.handle_ = OpenFileMappingW(access, FALSE, ipc::source_thumbnail_mapping_name);
  if (!result.handle_)
    throw std::runtime_error("OBS source thumbnail IPC is unavailable (Windows error " +
                             std::to_string(GetLastError()) + ")");
  result.mapping_ = static_cast<ipc::SourceThumbnailMapping*>(
      MapViewOfFile(result.handle_, access, 0, 0, sizeof(ipc::SourceThumbnailMapping)));
  if (!result.mapping_)
    throw std::runtime_error("Could not map OBS source thumbnail IPC");
  return result;
}

WindowsThumbnailMailbox::WindowsThumbnailMailbox(WindowsThumbnailMailbox&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      mapping_(std::exchange(other.mapping_, nullptr))
{
}

WindowsThumbnailMailbox::~WindowsThumbnailMailbox()
{
  if (mapping_ && !UnmapViewOfFile(mapping_))
    OutputDebugStringW(L"Could not unmap OBS source thumbnails\n");
  if (handle_ && !CloseHandle(handle_))
    OutputDebugStringW(L"Could not close OBS source thumbnails\n");
}

ipc::SourceThumbnailMapping& WindowsThumbnailMailbox::mapping()
{
  if (!mapping_)
    throw std::logic_error("OBS thumbnail mapping is not owned");
  return *mapping_;
}
} // namespace studio_remote::windows
