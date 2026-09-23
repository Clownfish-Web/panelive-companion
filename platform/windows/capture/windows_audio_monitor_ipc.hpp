#pragma once

#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/ipc/audio_monitor_reader.hpp>
#include <studio_remote/media/audio_monitor_connection.hpp>

#include <Windows.h>
#include <cstdint>
#include <optional>

namespace studio_remote::windows
{
/// One reader worker owns the paired views and reader state. Frames own their copied samples;
/// this resource never invokes callbacks or owns threads. Destruction releases partial opens too.
class WindowsAudioMonitorIpc final : public IAudioMonitorConnection
{
public:
  static std::optional<WindowsAudioMonitorIpc> open();
  ~WindowsAudioMonitorIpc() override;
  WindowsAudioMonitorIpc(WindowsAudioMonitorIpc&& other) noexcept;
  WindowsAudioMonitorIpc(const WindowsAudioMonitorIpc&) = delete;
  WindowsAudioMonitorIpc& operator=(const WindowsAudioMonitorIpc&) = delete;
  WindowsAudioMonitorIpc& operator=(WindowsAudioMonitorIpc&&) = delete;

  /// Invalidated mappings return nullopt so the worker can discard and reopen the pair.
  std::optional<ipc::AudioMonitorRead> read(std::uint64_t now_ns) override;
  bool publish_settings(const AudioMonitorMixSettings& settings) override;

private:
  WindowsAudioMonitorIpc() = default;
  bool compatible() const;

  HANDLE audio_handle_ = nullptr;
  const ipc::AudioMonitorMapping* audio_ = nullptr;
  HANDLE control_handle_ = nullptr;
  ipc::AudioMonitorControlMapping* control_ = nullptr;
  ipc::AudioMonitorReader reader_;
};
} // namespace studio_remote::windows
