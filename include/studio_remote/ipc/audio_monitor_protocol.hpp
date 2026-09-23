#pragma once

#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/media/audio_monitor.hpp>

#include <optional>
#include <string>

namespace studio_remote::ipc
{

struct AudioMonitorSequenceWindow
{
  std::uint64_t first_sequence = 0;
  std::uint64_t last_sequence = 0;
  std::uint64_t skipped_frames = 0;

  bool empty() const noexcept
  {
    return first_sequence == 0;
  }
};

std::optional<std::string> validate_audio_monitor_mapping(const AudioMonitorMapping& mapping);
bool compatible_audio_monitor_pair(const AudioMonitorMapping& audio,
                                   const AudioMonitorControlMapping& control);
/// Validate the pair and settings before any control store. Invalid headroom throws; an
/// unavailable/incompatible pair returns false without changing producer-owned resources.
bool publish_audio_monitor_settings(const AudioMonitorMapping& audio,
                                    AudioMonitorControlMapping& control,
                                    const AudioMonitorMixSettings& settings);
AudioMonitorSequenceWindow pending_audio_monitor_sequences(const AudioMonitorMapping& mapping,
                                                           std::uint64_t after_sequence);
std::optional<AudioMonitorFrame> copy_audio_monitor_frame(const AudioMonitorMapping& mapping,
                                                          std::uint64_t sequence);
bool audio_monitor_heartbeat_fresh(std::uint64_t observed_ns,
                                   std::uint64_t now_ns,
                                   std::uint64_t timeout_ns = 1'500'000'000ULL);

} // namespace studio_remote::ipc
