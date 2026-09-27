#pragma once

#include <studio_remote/ipc/video_capture_control_mapping.hpp>
#include <studio_remote/ipc/video_capture_demand_publisher.hpp>
#include <studio_remote/media/video_capture_demand.hpp>

#include "../ipc/linux_ipc_mapping.hpp"
#include <cstdint>
#include <optional>

namespace studio_remote::ipc
{
struct VideoCaptureControlMapping;
}

namespace studio_remote::linux
{
/// Single capture-worker owner of helper demand. Only an adopted valid producer receives final
/// idle demand before unmap. Failed and moved-from resources never write to the control mapping.
class LinuxVideoCaptureDemand final : public IVideoCaptureDemand
{
public:
  static std::optional<LinuxVideoCaptureDemand>
  open(std::optional<std::uint32_t> expected_producer = std::nullopt,
       std::string name = ipc::video_capture_control_shared_memory_name);
  ~LinuxVideoCaptureDemand() override;
  LinuxVideoCaptureDemand(LinuxVideoCaptureDemand&& other) noexcept;
  LinuxVideoCaptureDemand(const LinuxVideoCaptureDemand&) = delete;
  LinuxVideoCaptureDemand& operator=(const LinuxVideoCaptureDemand&) = delete;
  LinuxVideoCaptureDemand& operator=(LinuxVideoCaptureDemand&&) = delete;

  bool publish(VideoDemand demand, std::uint64_t now_ns) override;
  std::uint32_t producer_process_id() const noexcept override;
  std::uint64_t generation() const noexcept override;

private:
  explicit LinuxVideoCaptureDemand(IpcMapping memory);
  IpcMapping memory_;
  ipc::VideoCaptureControlMapping* mapping_ = nullptr;
  std::optional<ipc::VideoCaptureDemandPublisher> publisher_;
};
} // namespace studio_remote::linux
