#pragma once

#include <cstdint>

namespace studio_remote
{
enum class CapturedVideoTrack
{
  program,
  preview
};

/// An observation of independent producer fields, not a transaction across both tracks.
struct VideoProducerSnapshot
{
  std::uint32_t process_id = 0;
  std::uint64_t heartbeat_ns = 0;
  std::uint32_t program_generation = 0;
  std::uint32_t preview_generation = 0;
};

/// Sampled at activation after demand evaluation to suppress already-buffered frames.
struct VideoPublicationCounts
{
  std::uint64_t program = 0;
  std::uint64_t preview = 0;
};
} // namespace studio_remote
