#include "macos_capture_worker.hpp"

#include "macos_video_capture_demand.hpp"
#include "macos_video_frames.hpp"

#include <memory>
#include <syslog.h>
#include <time.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
void report_failure(const char* message) noexcept
{
  syslog(LOG_ERR, "%s", message);
}
std::uint64_t monotonic_time_ns()
{
  // Match libobs frame timestamps; libc++ steady_clock includes system sleep.
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
} // namespace

MacosCaptureWorker::MacosCaptureWorker(TrackHandler track_handler,
                                       HealthHandler health_handler,
                                       DemandProvider demand_provider,
                                       GenerationHandler generation_handler,
                                       std::string frame_name,
                                       std::string control_name,
                                       MonotonicClock clock)
    : worker_(
          [name = std::move(frame_name)]() -> std::unique_ptr<IVideoFrameSource>
          {
            auto frames = MacosVideoFrames::open(name);
            return frames ? std::make_unique<MacosVideoFrames>(std::move(*frames)) : nullptr;
          },
          [name = std::move(control_name)](
              std::uint32_t producer) -> std::unique_ptr<IVideoCaptureDemand>
          {
            auto control = MacosVideoCaptureDemand::open(name, producer);
            return control ? std::make_unique<MacosVideoCaptureDemand>(std::move(*control))
                           : nullptr;
          },
          clock ? std::move(clock) : MonotonicClock{monotonic_time_ns},
          report_failure,
          std::move(track_handler),
          std::move(health_handler),
          std::move(demand_provider),
          std::move(generation_handler))
{
}
MacosCaptureWorker::~MacosCaptureWorker() = default;
void MacosCaptureWorker::start()
{
  worker_.start();
}
void MacosCaptureWorker::stop()
{
  worker_.stop();
}
json MacosCaptureWorker::health() const
{
  return worker_.health();
}
} // namespace studio_remote::macos
