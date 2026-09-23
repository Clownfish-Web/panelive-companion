#pragma once

#include <algorithm>
#include <cstdint>

namespace studio_remote
{

class FrameCadence
{
public:
  bool should_publish(std::uint64_t timestamp_ns, std::uint32_t frames_per_second) noexcept
  {
    constexpr std::uint64_t clock_tolerance_ns = 1'000'000ULL;
    const auto interval_ns = 1'000'000'000ULL / std::clamp(frames_per_second, 1U, 15U);
    if (last_published_ns_ == 0)
    {
      last_published_ns_ = timestamp_ns;
      return true;
    }

    const auto due_ns = last_published_ns_ + interval_ns;
    if (timestamp_ns + clock_tolerance_ns < due_ns)
      return false;

    last_published_ns_ = due_ns;
    if (timestamp_ns > due_ns + interval_ns)
      last_published_ns_ = timestamp_ns;
    return true;
  }

private:
  std::uint64_t last_published_ns_ = 0;
};

} // namespace studio_remote
