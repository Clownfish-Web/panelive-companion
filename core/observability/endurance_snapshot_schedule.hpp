#pragma once

#include <chrono>

namespace studio_remote
{
/// Periodic wake policy, independent of whether a snapshot callback is currently installed.
/// The log worker owns this value. Explicit time points keep the policy deterministic.
class EnduranceSnapshotSchedule final
{
public:
  using Clock = std::chrono::steady_clock;
  EnduranceSnapshotSchedule(Clock::time_point now, std::chrono::milliseconds interval)
      : next_(now), interval_(interval)
  {
  }

  Clock::time_point deadline() const noexcept
  {
    return next_;
  }

  bool poll(Clock::time_point now) noexcept
  {
    if (now < next_)
      return false;
    next_ = now + interval_;
    return true;
  }

  void restart(Clock::time_point now) noexcept
  {
    next_ = now;
  }

private:
  Clock::time_point next_;
  const std::chrono::milliseconds interval_;
};
} // namespace studio_remote
