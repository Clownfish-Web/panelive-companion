#include "linux_audio_monitor.hpp"

#include "linux_audio_monitor_ipc.hpp"

#include <cstdio>
#include <memory>
#include <studio_remote/common/types.hpp>
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

LinuxAudioMonitor::LinuxAudioMonitor(FrameHandler frame_handler,
                                     StatusHandler status_handler,
                                     LinuxAudioMonitorIpcNames ipc_names)
    : worker_(
          [names = std::move(ipc_names)]() -> std::unique_ptr<IAudioMonitorConnection>
          {
            auto resources = LinuxAudioMonitorIpc::open(names);
            return resources ? std::make_unique<LinuxAudioMonitorIpc>(std::move(*resources))
                             : nullptr;
          },
          monotonic_ns,
          std::move(frame_handler),
          std::move(status_handler),
          nullptr,
          report_failure)
{
}
LinuxAudioMonitor::~LinuxAudioMonitor() = default;
void LinuxAudioMonitor::start()
{
  worker_.start();
}
void LinuxAudioMonitor::stop()
{
  worker_.stop();
}
AudioMonitorStatus LinuxAudioMonitor::status() const
{
  return worker_.status();
}
void LinuxAudioMonitor::set_mix_settings(const AudioMonitorMixSettings& settings)
{
  worker_.set_mix_settings(settings);
}
} // namespace studio_remote::linux
