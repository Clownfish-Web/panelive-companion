#pragma once

#include "../ipc/macos_ipc_names.hpp"
#include "../ipc/read_only_shared_memory.hpp"
#include "../ipc/writable_shared_memory.hpp"

#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/ipc/audio_monitor_reader.hpp>
#include <studio_remote/media/audio_monitor.hpp>
#include <studio_remote/media/audio_monitor_connection.hpp>

namespace studio_remote::macos
{
/// A single reader worker owns each resource pair and all sequence/heartbeat state. Returned
/// frames own their samples independently. This object never invokes callbacks or opens threads.
class MacosAudioMonitorIpc final : public IAudioMonitorConnection
{
public:
  static std::optional<MacosAudioMonitorIpc>
  open(std::string_view audio_name = ipc_names::audio_monitor,
       std::string_view control_name = ipc_names::audio_monitor_control);
  MacosAudioMonitorIpc(MacosAudioMonitorIpc&&) noexcept = default;
  MacosAudioMonitorIpc(const MacosAudioMonitorIpc&) = delete;
  MacosAudioMonitorIpc& operator=(const MacosAudioMonitorIpc&) = delete;

  /// Invalidated headers return nullopt so the worker can discard and reopen both resources.
  std::optional<ipc::AudioMonitorRead> read(std::uint64_t now_ns) override;
  bool publish_settings(const AudioMonitorMixSettings& settings) override;

private:
  MacosAudioMonitorIpc(ReadOnlySharedMemory audio, WritableSharedMemory control);
  const ipc::AudioMonitorMapping& mapping() const;
  ipc::AudioMonitorControlMapping& control();
  bool compatible();

  ReadOnlySharedMemory audio_;
  WritableSharedMemory control_;
  ipc::AudioMonitorReader reader_;
};
} // namespace studio_remote::macos
