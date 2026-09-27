#pragma once

#include <studio_remote/ipc/video_capture_demand_publisher.hpp>
#include <studio_remote/media/video_capture_demand.hpp>

#include "../ipc/windows_ipc_mapping.hpp"
#include <cstdint>
#include <optional>

namespace studio_remote::ipc
{
struct VideoCaptureControlMapping;
}

namespace studio_remote::windows
{
/// Single capture-worker owner of helper demand. Only an adopted valid producer receives final
/// idle demand before unmap. Failed and moved-from resources never write to the control mapping.
class WindowsVideoCaptureDemand final : public IVideoCaptureDemand
{
public:
  static std::optional<WindowsVideoCaptureDemand>
  open(std::optional<std::uint32_t> expected_producer = std::nullopt);
  ~WindowsVideoCaptureDemand() override;
  WindowsVideoCaptureDemand(WindowsVideoCaptureDemand&& other) noexcept;
  WindowsVideoCaptureDemand(const WindowsVideoCaptureDemand&) = delete;
  WindowsVideoCaptureDemand& operator=(const WindowsVideoCaptureDemand&) = delete;
  WindowsVideoCaptureDemand& operator=(WindowsVideoCaptureDemand&&) = delete;

  bool publish(VideoDemand demand, std::uint64_t now_ns) override;
  std::uint32_t producer_process_id() const noexcept override;
  std::uint64_t generation() const noexcept override;

private:
  WindowsVideoCaptureDemand() = default;
  IpcMappingHandle handle_;
  ipc::VideoCaptureControlMapping* mapping_ = nullptr;
  std::optional<ipc::VideoCaptureDemandPublisher> publisher_;
};
} // namespace studio_remote::windows
