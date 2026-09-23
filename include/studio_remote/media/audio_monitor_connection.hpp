#pragma once

#include <studio_remote/ipc/audio_monitor_reader.hpp>
#include <studio_remote/media/audio_monitor_mix_settings.hpp>

#include <cstdint>
#include <optional>

namespace studio_remote
{
/// One audio worker owns a connection and serializes its operations. Implementations own native
/// resources; returned frames own independent samples and remain valid after connection release.
class IAudioMonitorConnection
{
public:
  virtual ~IAudioMonitorConnection() = default;
  virtual bool publish_settings(const AudioMonitorMixSettings& settings) = 0;
  virtual std::optional<ipc::AudioMonitorRead> read(std::uint64_t now_ns) = 0;
};
} // namespace studio_remote
