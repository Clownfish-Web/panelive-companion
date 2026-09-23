#include "windows_audio_monitor_ipc.hpp"

#include <studio_remote/ipc/audio_monitor_protocol.hpp>

#include <utility>

namespace studio_remote::windows
{
std::optional<WindowsAudioMonitorIpc> WindowsAudioMonitorIpc::open()
{
  WindowsAudioMonitorIpc result;
  result.audio_handle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, ipc::audio_monitor_mapping_name);
  if (!result.audio_handle_)
    return std::nullopt;
  result.audio_ = static_cast<const ipc::AudioMonitorMapping*>(
      MapViewOfFile(result.audio_handle_, FILE_MAP_READ, 0, 0, sizeof(ipc::AudioMonitorMapping)));
  if (!result.audio_)
    return std::nullopt;
  constexpr DWORD control_access = FILE_MAP_READ | FILE_MAP_WRITE;
  result.control_handle_ =
      OpenFileMappingW(control_access, FALSE, ipc::audio_monitor_control_mapping_name);
  if (!result.control_handle_)
    return std::nullopt;
  result.control_ = static_cast<ipc::AudioMonitorControlMapping*>(MapViewOfFile(
      result.control_handle_, control_access, 0, 0, sizeof(ipc::AudioMonitorControlMapping)));
  if (!result.compatible())
    return std::nullopt;
  result.reader_ =
      ipc::AudioMonitorReader(result.audio_->published_sequence.load(std::memory_order_acquire));
  return result;
}

WindowsAudioMonitorIpc::~WindowsAudioMonitorIpc()
{
  if (control_ && !UnmapViewOfFile(control_))
    OutputDebugStringW(L"Could not unmap OBS audio monitor control\n");
  if (control_handle_ && !CloseHandle(control_handle_))
    OutputDebugStringW(L"Could not close OBS audio monitor control\n");
  if (audio_ && !UnmapViewOfFile(audio_))
    OutputDebugStringW(L"Could not unmap OBS audio monitor frames\n");
  if (audio_handle_ && !CloseHandle(audio_handle_))
    OutputDebugStringW(L"Could not close OBS audio monitor frames\n");
}

WindowsAudioMonitorIpc::WindowsAudioMonitorIpc(WindowsAudioMonitorIpc&& other) noexcept
    : audio_handle_(std::exchange(other.audio_handle_, nullptr)),
      audio_(std::exchange(other.audio_, nullptr)),
      control_handle_(std::exchange(other.control_handle_, nullptr)),
      control_(std::exchange(other.control_, nullptr)), reader_(std::move(other.reader_))
{
}

bool WindowsAudioMonitorIpc::compatible() const
{
  return audio_ && control_ && ipc::compatible_audio_monitor_pair(*audio_, *control_);
}

bool WindowsAudioMonitorIpc::publish_settings(const AudioMonitorMixSettings& settings)
{
  if (!audio_ || !control_)
    return false;
  return ipc::publish_audio_monitor_settings(*audio_, *control_, settings);
}

std::optional<ipc::AudioMonitorRead> WindowsAudioMonitorIpc::read(std::uint64_t now_ns)
{
  if (!compatible())
    return std::nullopt;
  return reader_.read(*audio_, now_ns);
}
} // namespace studio_remote::windows
