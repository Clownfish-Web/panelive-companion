#include "windows_audio_monitor.hpp"

#include "windows_audio_monitor_ipc.hpp"

#include <Windows.h>
#include <memory>
#include <studio_remote/common/types.hpp>
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

WindowsAudioMonitor::WindowsAudioMonitor(FrameHandler frame_handler,
                                         StatusHandler status_handler,
                                         RealtimeAudioServices* realtime_audio)
    : worker_(
          []() -> std::unique_ptr<IAudioMonitorConnection>
          {
            auto resources = WindowsAudioMonitorIpc::open();
            return resources ? std::make_unique<WindowsAudioMonitorIpc>(std::move(*resources))
                             : nullptr;
          },
          monotonic_ns,
          std::move(frame_handler),
          std::move(status_handler),
          realtime_audio,
          report_failure)
{
}
WindowsAudioMonitor::~WindowsAudioMonitor() = default;
void WindowsAudioMonitor::start()
{
  worker_.start();
}
void WindowsAudioMonitor::stop()
{
  worker_.stop();
}
AudioMonitorStatus WindowsAudioMonitor::status() const
{
  return worker_.status();
}
void WindowsAudioMonitor::set_mix_settings(const AudioMonitorMixSettings& settings)
{
  worker_.set_mix_settings(settings);
}
} // namespace studio_remote::windows
