#include "windows_capture.hpp"

#include "windows_video_capture_demand.hpp"
#include "windows_video_frames.hpp"

#include <Windows.h>
#include <memory>
#include <utility>

namespace studio_remote::windows
{
namespace
{
void report_failure(const char* message) noexcept
{
  OutputDebugStringA(message);
  OutputDebugStringA("\n");
}
} // namespace

CaptureManager::CaptureManager(TrackHandler track_handler,
                               HealthHandler health_handler,
                               DemandProvider demand_provider,
                               GenerationHandler generation_handler)
    : worker_(
          []() -> std::unique_ptr<IVideoFrameSource>
          {
            auto frames = WindowsVideoFrames::open();
            return frames ? std::make_unique<WindowsVideoFrames>(std::move(*frames)) : nullptr;
          },
          [](std::uint32_t producer) -> std::unique_ptr<IVideoCaptureDemand>
          {
            auto control = WindowsVideoCaptureDemand::open(producer);
            return control ? std::make_unique<WindowsVideoCaptureDemand>(std::move(*control))
                           : nullptr;
          },
          monotonic_ns,
          report_failure,
          std::move(track_handler),
          std::move(health_handler),
          std::move(demand_provider),
          std::move(generation_handler))
{
}
CaptureManager::~CaptureManager() = default;
void CaptureManager::start()
{
  worker_.start();
}
void CaptureManager::stop()
{
  worker_.stop();
}
json CaptureManager::health() const
{
  return worker_.health();
}
} // namespace studio_remote::windows
