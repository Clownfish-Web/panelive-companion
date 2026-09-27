#include "linux_capture.hpp"

#include "linux_video_capture_demand.hpp"
#include "linux_video_frames.hpp"

#include <cstdio>
#include <memory>
#include <utility>

namespace studio_remote::linux
{
namespace
{
void report_failure(const char* message) noexcept
{
  std::fprintf(stderr, "%s\n", message);
}
} // namespace

CaptureManager::CaptureManager(TrackHandler track_handler,
                               HealthHandler health_handler,
                               DemandProvider demand_provider,
                               GenerationHandler generation_handler)
    : worker_(
          []() -> std::unique_ptr<IVideoFrameSource>
          {
            auto frames = LinuxVideoFrames::open();
            return frames ? std::make_unique<LinuxVideoFrames>(std::move(*frames)) : nullptr;
          },
          [](std::uint32_t producer) -> std::unique_ptr<IVideoCaptureDemand>
          {
            auto control = LinuxVideoCaptureDemand::open(producer);
            return control ? std::make_unique<LinuxVideoCaptureDemand>(std::move(*control))
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
} // namespace studio_remote::linux
