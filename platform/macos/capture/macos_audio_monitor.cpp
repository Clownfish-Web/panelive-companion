#include "macos_audio_monitor.hpp"

#include "macos_audio_monitor_ipc.hpp"

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
  // Match libobs timestamps; libc++ steady_clock includes system sleep.
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
} // namespace

MacosAudioMonitor::MacosAudioMonitor(FrameHandler frame_handler,
                                     StatusHandler status_handler,
                                     RealtimeAudioServices* realtime_audio,
                                     std::string audio_name,
                                     std::string control_name,
                                     MonotonicClock clock)
    : worker_(
          [audio = std::move(audio_name),
           control = std::move(control_name)]() -> std::unique_ptr<IAudioMonitorConnection>
          {
            auto resources = MacosAudioMonitorIpc::open(audio, control);
            return resources ? std::make_unique<MacosAudioMonitorIpc>(std::move(*resources))
                             : nullptr;
          },
          clock ? std::move(clock) : MonotonicClock{monotonic_time_ns},
          std::move(frame_handler),
          std::move(status_handler),
          realtime_audio,
          report_failure)
{
}
MacosAudioMonitor::~MacosAudioMonitor() = default;
void MacosAudioMonitor::start()
{
  worker_.start();
}
void MacosAudioMonitor::stop()
{
  worker_.stop();
}
AudioMonitorStatus MacosAudioMonitor::status() const
{
  return worker_.status();
}
void MacosAudioMonitor::set_mix_settings(const AudioMonitorMixSettings& settings)
{
  worker_.set_mix_settings(settings);
}
} // namespace studio_remote::macos
