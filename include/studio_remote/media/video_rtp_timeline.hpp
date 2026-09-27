#pragma once

#include <cstdint>

namespace studio_remote
{

// Owned by one encoder thread. Preserve the capture clock across source generation changes;
// only an actual clock reset needs an offset to retain forward RTP progression.
class VideoRtpTimeline
{
public:
  std::uint32_t map(std::uint64_t source_ns,
                    std::uint64_t source_generation,
                    std::uint32_t nominal_duration) noexcept;

private:
  std::uint64_t source_generation_ = 0;
  std::uint64_t last_source_ns_ = 0;
  std::uint32_t offset_ = 0;
  std::uint32_t last_timestamp_ = 0;
  bool started_ = false;
};

} // namespace studio_remote
