#pragma once

#include <chrono>
#include <cstdint>

namespace studio_remote
{
/// Deterministic retry policy. The caller supplies connection age and native randomness.
/// Failed/short attempts increase the base from 1 to 30 seconds. Ten stable seconds reset it.
class ObsReconnectBackoff final
{
public:
  std::chrono::milliseconds next_delay(std::chrono::milliseconds connection_age,
                                       std::uint32_t random_sample) noexcept;
  void reset() noexcept;

private:
  std::chrono::milliseconds base_{1000};
};
} // namespace studio_remote
