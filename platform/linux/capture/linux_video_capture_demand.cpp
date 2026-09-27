#include "linux_video_capture_demand.hpp"

#include <studio_remote/common/types.hpp>
#include <studio_remote/ipc/video_capture_control_mapping.hpp>

#include <utility>

namespace studio_remote::linux
{
std::optional<LinuxVideoCaptureDemand>
LinuxVideoCaptureDemand::open(std::optional<std::uint32_t> expected_producer, std::string name)
{
  LinuxVideoCaptureDemand result(
      IpcMapping::open(name, sizeof(ipc::VideoCaptureControlMapping), IpcAccess::read_write));
  if (!result.mapping_)
    return std::nullopt;
  result.publisher_ = ipc::VideoCaptureDemandPublisher::open(*result.mapping_, expected_producer);
  if (!result.publisher_)
    return std::nullopt;
  return result;
}

LinuxVideoCaptureDemand::LinuxVideoCaptureDemand(IpcMapping memory)
    : memory_(std::move(memory)),
      mapping_(static_cast<ipc::VideoCaptureControlMapping*>(memory_.get()))
{
}

LinuxVideoCaptureDemand::LinuxVideoCaptureDemand(LinuxVideoCaptureDemand&& other) noexcept
    : memory_(std::move(other.memory_)), mapping_(std::exchange(other.mapping_, nullptr)),
      publisher_(std::exchange(other.publisher_, std::nullopt))
{
}

LinuxVideoCaptureDemand::~LinuxVideoCaptureDemand()
{
  if (mapping_ && publisher_)
    publisher_->publish_idle(*mapping_, monotonic_ns());
}

bool LinuxVideoCaptureDemand::publish(VideoDemand demand, std::uint64_t now_ns)
{
  return mapping_ && publisher_ && publisher_->publish(*mapping_, demand, now_ns);
}

std::uint32_t LinuxVideoCaptureDemand::producer_process_id() const noexcept
{
  return publisher_ ? publisher_->producer_process_id() : 0;
}

std::uint64_t LinuxVideoCaptureDemand::generation() const noexcept
{
  return publisher_ ? publisher_->generation() : 0;
}
} // namespace studio_remote::linux
