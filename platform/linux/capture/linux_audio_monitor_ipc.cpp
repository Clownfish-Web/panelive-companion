#include "linux_audio_monitor_ipc.hpp"

#include <studio_remote/ipc/audio_monitor_protocol.hpp>

#include <utility>

namespace studio_remote::linux
{
std::optional<LinuxAudioMonitorIpc>
LinuxAudioMonitorIpc::open(const LinuxAudioMonitorIpcNames& names)
{
  auto audio =
      IpcMapping::open(names.audio, sizeof(ipc::AudioMonitorMapping), IpcAccess::read_only);
  if (!audio.get())
    return std::nullopt;
  auto control = IpcMapping::open(
      names.control, sizeof(ipc::AudioMonitorControlMapping), IpcAccess::read_write);
  LinuxAudioMonitorIpc result(std::move(audio), std::move(control));
  if (!result.compatible())
    return std::nullopt;
  result.reader_ =
      ipc::AudioMonitorReader(result.audio_->published_sequence.load(std::memory_order_acquire));
  return result;
}

LinuxAudioMonitorIpc::LinuxAudioMonitorIpc(IpcMapping audio, IpcMapping control)
    : audio_memory_(std::move(audio)),
      audio_(static_cast<const ipc::AudioMonitorMapping*>(audio_memory_.get())),
      control_memory_(std::move(control)),
      control_(static_cast<ipc::AudioMonitorControlMapping*>(control_memory_.get()))
{
}
LinuxAudioMonitorIpc::~LinuxAudioMonitorIpc() = default;

LinuxAudioMonitorIpc::LinuxAudioMonitorIpc(LinuxAudioMonitorIpc&& other) noexcept
    : audio_memory_(std::move(other.audio_memory_)), audio_(std::exchange(other.audio_, nullptr)),
      control_memory_(std::move(other.control_memory_)),
      control_(std::exchange(other.control_, nullptr)), reader_(std::move(other.reader_))
{
}

bool LinuxAudioMonitorIpc::compatible() const
{
  return audio_ && control_ && ipc::compatible_audio_monitor_pair(*audio_, *control_);
}

bool LinuxAudioMonitorIpc::publish_settings(const AudioMonitorMixSettings& settings)
{
  if (!audio_ || !control_)
    return false;
  return ipc::publish_audio_monitor_settings(*audio_, *control_, settings);
}

std::optional<ipc::AudioMonitorRead> LinuxAudioMonitorIpc::read(std::uint64_t now_ns)
{
  if (!compatible())
    return std::nullopt;
  return reader_.read(*audio_, now_ns);
}
} // namespace studio_remote::linux
