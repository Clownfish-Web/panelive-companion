#include "windows_video_capture_demand.hpp"

#include <studio_remote/common/types.hpp>
#include <studio_remote/ipc/video_capture_control_mapping.hpp>

#include <utility>

namespace studio_remote::windows
{
std::optional<WindowsVideoCaptureDemand>
WindowsVideoCaptureDemand::open(std::optional<std::uint32_t> expected_producer)
{
  WindowsVideoCaptureDemand result;
  constexpr DWORD access = FILE_MAP_READ | FILE_MAP_WRITE;
  result.handle_ = open_owner_restricted_mapping(access, ipc::video_capture_control_mapping_name);
  if (!result.handle_)
    return std::nullopt;
  result.mapping_ = static_cast<ipc::VideoCaptureControlMapping*>(
      MapViewOfFile(result.handle_.get(), access, 0, 0, sizeof(ipc::VideoCaptureControlMapping)));
  if (!result.mapping_)
    return std::nullopt;
  result.publisher_ = ipc::VideoCaptureDemandPublisher::open(*result.mapping_, expected_producer);
  if (!result.publisher_)
    return std::nullopt;
  return result;
}

WindowsVideoCaptureDemand::WindowsVideoCaptureDemand(WindowsVideoCaptureDemand&& other) noexcept
    : handle_(std::move(other.handle_)), mapping_(std::exchange(other.mapping_, nullptr)),
      publisher_(std::exchange(other.publisher_, std::nullopt))
{
}

WindowsVideoCaptureDemand::~WindowsVideoCaptureDemand()
{
  if (mapping_ && publisher_)
    publisher_->publish_idle(*mapping_, monotonic_ns());
  if (mapping_ && !UnmapViewOfFile(mapping_))
    OutputDebugStringW(L"Could not unmap OBS video capture control\n");
}

bool WindowsVideoCaptureDemand::publish(VideoDemand demand, std::uint64_t now_ns)
{
  return mapping_ && publisher_ && publisher_->publish(*mapping_, demand, now_ns);
}

std::uint32_t WindowsVideoCaptureDemand::producer_process_id() const noexcept
{
  return publisher_ ? publisher_->producer_process_id() : 0;
}

std::uint64_t WindowsVideoCaptureDemand::generation() const noexcept
{
  return publisher_ ? publisher_->generation() : 0;
}
} // namespace studio_remote::windows
