#pragma once

#include "../ipc/macos_ipc_names.hpp"
#include "../ipc/writable_shared_memory.hpp"

#include <optional>
#include <studio_remote/ipc/video_capture_demand_publisher.hpp>
#include <studio_remote/media/video_capture_demand.hpp>

namespace studio_remote::macos
{
/// Single capture-worker owner of helper demand. Destruction publishes idle demand before unmap.
/// The capture composition must validate the paired frame/control producer identity before use.
class MacosVideoCaptureDemand final : public IVideoCaptureDemand
{
public:
  static std::optional<MacosVideoCaptureDemand>
  open(std::string_view name = ipc_names::video_capture_control,
       std::optional<std::uint32_t> expected_producer = std::nullopt);
  ~MacosVideoCaptureDemand() override;
  MacosVideoCaptureDemand(MacosVideoCaptureDemand&& other) noexcept;
  MacosVideoCaptureDemand(const MacosVideoCaptureDemand&) = delete;
  MacosVideoCaptureDemand& operator=(const MacosVideoCaptureDemand&) = delete;
  MacosVideoCaptureDemand& operator=(MacosVideoCaptureDemand&&) = delete;
  bool publish(VideoDemand demand, std::uint64_t now_ns) override;
  std::uint32_t producer_process_id() const noexcept override;
  std::uint64_t generation() const noexcept override;

private:
  MacosVideoCaptureDemand(WritableSharedMemory region, ipc::VideoCaptureDemandPublisher publisher);
  WritableSharedMemory region_;
  std::optional<ipc::VideoCaptureDemandPublisher> publisher_;
};
} // namespace studio_remote::macos
