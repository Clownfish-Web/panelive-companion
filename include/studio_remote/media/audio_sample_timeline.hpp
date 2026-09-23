#pragma once

#include <cstdint>

namespace studio_remote
{

struct AudioSamplePosition
{
  std::uint64_t bucket_timestamp_ns = 0;
  std::uint32_t sample_index = 0;
};

// Maps a callback's first timestamp once, then advances in the sample domain. This avoids the
// duplicate indexes produced by converting every truncated sample timestamp back to frames.
constexpr AudioSamplePosition audio_sample_position(std::uint64_t first_timestamp_ns,
                                                    std::uint32_t callback_sample_offset,
                                                    std::uint32_t sample_rate,
                                                    std::uint32_t bucket_samples)
{
  if (sample_rate == 0 || bucket_samples == 0)
    return {};
  const auto bucket_duration_ns =
      static_cast<std::uint64_t>(bucket_samples) * 1'000'000'000ULL / sample_rate;
  if (bucket_duration_ns == 0)
    return {};
  const auto first_bucket = first_timestamp_ns - first_timestamp_ns % bucket_duration_ns;
  const auto within_bucket_ns = first_timestamp_ns - first_bucket;
  const auto first_sample = (within_bucket_ns * sample_rate + 500'000'000ULL) / 1'000'000'000ULL;
  const auto absolute_sample = first_sample + callback_sample_offset;
  return {first_bucket + absolute_sample / bucket_samples * bucket_duration_ns,
          static_cast<std::uint32_t>(absolute_sample % bucket_samples)};
}

} // namespace studio_remote
