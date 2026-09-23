#pragma once

#include <studio_remote/media/audio_monitor.hpp>

#include <cstdint>
#include <vector>

namespace studio_remote::ipc
{
struct AudioMonitorMapping;

struct AudioMonitorRead
{
  AudioMonitorStatus status;
  std::vector<SharedAudioMonitorFrame> frames;
};

/// One worker owns this state for one mapping lifetime. Reads synchronously borrow a validated
/// mapping; returned frames own their samples. No mapping, callback, or native resource is
/// retained.
class AudioMonitorReader final
{
public:
  /// Seed from the current published sequence on open to skip preexisting ring history.
  explicit AudioMonitorReader(std::uint64_t initial_sequence = 0);
  /// The caller validates the mapping and supplies time in the producer's captured_ns domain.
  AudioMonitorRead read(const AudioMonitorMapping& mapping, std::uint64_t now_ns);

private:
  void copy_pending_frames(const AudioMonitorMapping& mapping,
                           std::vector<SharedAudioMonitorFrame>& frames);
  AudioMonitorStatus
  status(const AudioMonitorMapping& mapping, std::uint64_t now, bool fresh, bool configured) const;

  std::uint64_t last_sequence_ = 0;
  std::uint64_t last_heartbeat_ = 0;
  std::uint64_t heartbeat_seen_ns_ = 0;
  std::uint64_t last_frame_captured_ns_ = 0;
  std::uint64_t ipc_skipped_frames_ = 0;
  std::uint64_t ipc_sample_index_gaps_ = 0;
  std::uint64_t ipc_seqlock_retries_ = 0;
  std::uint64_t next_sample_index_ = 0;
  std::uint32_t last_topology_generation_ = 0;
};
} // namespace studio_remote::ipc
