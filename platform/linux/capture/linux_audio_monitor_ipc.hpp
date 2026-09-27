#pragma once

#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/ipc/audio_monitor_reader.hpp>
#include <studio_remote/media/audio_monitor_connection.hpp>

#include "../ipc/linux_ipc_mapping.hpp"
#include <cstdint>
#include <optional>

namespace studio_remote::linux
{
struct LinuxAudioMonitorIpcNames
{
  std::string audio = ipc::audio_monitor_shared_memory_name;
  std::string control = ipc::audio_monitor_control_shared_memory_name;
};

/// One reader worker owns the paired views and reader state. Frames own their copied samples;
/// this resource never invokes callbacks or owns threads. Destruction releases partial opens too.
class LinuxAudioMonitorIpc final : public IAudioMonitorConnection
{
public:
  static std::optional<LinuxAudioMonitorIpc> open(const LinuxAudioMonitorIpcNames& names = {});
  ~LinuxAudioMonitorIpc() override;
  LinuxAudioMonitorIpc(LinuxAudioMonitorIpc&& other) noexcept;
  LinuxAudioMonitorIpc(const LinuxAudioMonitorIpc&) = delete;
  LinuxAudioMonitorIpc& operator=(const LinuxAudioMonitorIpc&) = delete;
  LinuxAudioMonitorIpc& operator=(LinuxAudioMonitorIpc&&) = delete;

  /// Invalidated mappings return nullopt so the worker can discard and reopen the pair.
  std::optional<ipc::AudioMonitorRead> read(std::uint64_t now_ns) override;
  bool publish_settings(const AudioMonitorMixSettings& settings) override;

private:
  LinuxAudioMonitorIpc(IpcMapping audio, IpcMapping control);
  bool compatible() const;

  IpcMapping audio_memory_;
  const ipc::AudioMonitorMapping* audio_ = nullptr;
  IpcMapping control_memory_;
  ipc::AudioMonitorControlMapping* control_ = nullptr;
  ipc::AudioMonitorReader reader_;
};
} // namespace studio_remote::linux
