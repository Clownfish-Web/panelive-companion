#include <studio_remote/media/video_rtp_timeline.hpp>

namespace studio_remote
{

std::uint32_t VideoRtpTimeline::map(std::uint64_t source_ns,
                                    std::uint64_t source_generation,
                                    std::uint32_t nominal_duration) noexcept
{
  const auto seconds = source_ns / 1'000'000'000ULL;
  const auto remainder = source_ns % 1'000'000'000ULL;
  const auto source_timestamp =
      static_cast<std::uint32_t>(seconds * 90'000ULL + remainder * 90'000ULL / 1'000'000'000ULL);
  if (!started_)
  {
    started_ = true;
    source_generation_ = source_generation;
    last_source_ns_ = source_ns;
    last_timestamp_ = source_timestamp;
    return last_timestamp_;
  }

  if (source_generation_ != source_generation)
  {
    source_generation_ = source_generation;
    // OBS helper reloads do not reset the monotonic capture clock. Rebasing each track by
    // its different frame duration compresses downtime and accumulates cross-track skew.
    if (source_ns <= last_source_ns_)
      offset_ = last_timestamp_ + nominal_duration - source_timestamp;
  }

  auto timestamp = source_timestamp + offset_;
  const auto forward_distance = timestamp - last_timestamp_;
  if (forward_distance == 0 || forward_distance >= 0x80000000U)
    timestamp = last_timestamp_ + nominal_duration;
  last_timestamp_ = timestamp;
  last_source_ns_ = source_ns;
  return timestamp;
}

} // namespace studio_remote
