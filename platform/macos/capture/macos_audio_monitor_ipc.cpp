#include "macos_audio_monitor_ipc.hpp"

#include <studio_remote/ipc/audio_monitor_protocol.hpp>

#include <utility>

namespace studio_remote::macos
{
std::optional<MacosAudioMonitorIpc> MacosAudioMonitorIpc::open(std::string_view audio_name,
                                                               std::string_view control_name)
{
  auto audio = ReadOnlySharedMemory::open(audio_name, sizeof(ipc::AudioMonitorMapping));
  if (!audio)
    return std::nullopt;
  auto control = WritableSharedMemory::open(control_name, sizeof(ipc::AudioMonitorControlMapping));
  if (!control)
    return std::nullopt;
  MacosAudioMonitorIpc result(std::move(*audio), std::move(*control));
  if (!result.compatible())
    return std::nullopt;
  result.reader_ =
      ipc::AudioMonitorReader(result.mapping().published_sequence.load(std::memory_order_acquire));
  return result;
}

MacosAudioMonitorIpc::MacosAudioMonitorIpc(ReadOnlySharedMemory audio, WritableSharedMemory control)
    : audio_(std::move(audio)), control_(std::move(control))
{
}

const ipc::AudioMonitorMapping& MacosAudioMonitorIpc::mapping() const
{
  return *reinterpret_cast<const ipc::AudioMonitorMapping*>(audio_.bytes().data());
}
ipc::AudioMonitorControlMapping& MacosAudioMonitorIpc::control()
{
  return *reinterpret_cast<ipc::AudioMonitorControlMapping*>(control_.bytes().data());
}
bool MacosAudioMonitorIpc::compatible()
{
  return ipc::compatible_audio_monitor_pair(mapping(), control());
}

bool MacosAudioMonitorIpc::publish_settings(const AudioMonitorMixSettings& settings)
{
  return ipc::publish_audio_monitor_settings(mapping(), control(), settings);
}

std::optional<ipc::AudioMonitorRead> MacosAudioMonitorIpc::read(std::uint64_t now_ns)
{
  if (!compatible())
    return std::nullopt;
  return reader_.read(mapping(), now_ns);
}
} // namespace studio_remote::macos
