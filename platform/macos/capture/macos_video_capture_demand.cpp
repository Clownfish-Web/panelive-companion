#include "macos_video_capture_demand.hpp"

#include <studio_remote/ipc/video_capture_control_mapping.hpp>

#include <time.h>
#include <utility>

namespace studio_remote::macos
{
std::optional<MacosVideoCaptureDemand>
MacosVideoCaptureDemand::open(std::string_view name, std::optional<std::uint32_t> expected_producer)
{
  auto region = WritableSharedMemory::open(name, sizeof(ipc::VideoCaptureControlMapping));
  if (!region)
    return std::nullopt;
  const auto* mapping =
      reinterpret_cast<const ipc::VideoCaptureControlMapping*>(region->bytes().data());
  auto publisher = ipc::VideoCaptureDemandPublisher::open(*mapping, expected_producer);
  if (!publisher)
    return std::nullopt;
  return MacosVideoCaptureDemand(std::move(*region), std::move(*publisher));
}

MacosVideoCaptureDemand::MacosVideoCaptureDemand(WritableSharedMemory region,
                                                 ipc::VideoCaptureDemandPublisher publisher)
    : region_(std::move(region)), publisher_(std::move(publisher))
{
}

MacosVideoCaptureDemand::MacosVideoCaptureDemand(MacosVideoCaptureDemand&& other) noexcept
    : region_(std::move(other.region_)), publisher_(std::exchange(other.publisher_, std::nullopt))
{
}

MacosVideoCaptureDemand::~MacosVideoCaptureDemand()
{
  if (!region_.bytes().empty() && publisher_)
  {
    auto& mapping = *reinterpret_cast<ipc::VideoCaptureControlMapping*>(region_.bytes().data());
    publisher_->publish_idle(mapping, clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
  }
}

std::uint32_t MacosVideoCaptureDemand::producer_process_id() const noexcept
{
  return publisher_ ? publisher_->producer_process_id() : 0;
}

std::uint64_t MacosVideoCaptureDemand::generation() const noexcept
{
  return publisher_ ? publisher_->generation() : 0;
}

bool MacosVideoCaptureDemand::publish(VideoDemand demand, std::uint64_t now_ns)
{
  if (region_.bytes().empty() || !publisher_)
    return false;
  auto& mapping = *reinterpret_cast<ipc::VideoCaptureControlMapping*>(region_.bytes().data());
  return publisher_->publish(mapping, demand, now_ns);
}
} // namespace studio_remote::macos
