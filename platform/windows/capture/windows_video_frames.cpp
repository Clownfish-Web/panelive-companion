#include "windows_video_frames.hpp"

#include <utility>

namespace studio_remote::windows
{
std::optional<WindowsVideoFrames> WindowsVideoFrames::open()
{
  WindowsVideoFrames result;
  result.handle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, ipc::video_frame_mapping_name);
  if (!result.handle_)
    return std::nullopt;
  result.mapping_ = static_cast<const ipc::VideoFrameMapping*>(
      MapViewOfFile(result.handle_, FILE_MAP_READ, 0, 0, sizeof(ipc::VideoFrameMapping)));
  if (!result.mapping_ || !ipc::video_frame_mapping_is_compatible(*result.mapping_))
    return std::nullopt;
  return result;
}

WindowsVideoFrames::WindowsVideoFrames(WindowsVideoFrames&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      mapping_(std::exchange(other.mapping_, nullptr))
{
}

WindowsVideoFrames::~WindowsVideoFrames()
{
  if (mapping_ && !UnmapViewOfFile(mapping_))
    OutputDebugStringW(L"Could not unmap OBS video frames\n");
  if (handle_ && !CloseHandle(handle_))
    OutputDebugStringW(L"Could not close OBS video frames\n");
}

std::optional<VideoProducerSnapshot> WindowsVideoFrames::producer() const
{
  return mapping_ ? ipc::read_video_producer(*mapping_) : std::nullopt;
}

std::optional<VideoPublicationCounts> WindowsVideoFrames::published_frames() const
{
  return mapping_ ? ipc::read_video_publication_counts(*mapping_) : std::nullopt;
}

ipc::VideoFrameReadResult WindowsVideoFrames::read(CapturedVideoTrack track,
                                                   ipc::VideoSlotSequenceTracker& sequences,
                                                   std::uint64_t activation_published_frames) const
{
  return mapping_ ? ipc::read_video_frame(*mapping_, track, sequences, activation_published_frames)
                  : ipc::VideoFrameReadResult{};
}
} // namespace studio_remote::windows
