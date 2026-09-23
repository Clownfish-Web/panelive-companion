#include <studio_remote/obs/obs_reconnect.hpp>

#include <algorithm>
#include <limits>

namespace studio_remote
{
std::chrono::milliseconds ObsReconnectBackoff::next_delay(std::chrono::milliseconds connection_age,
                                                          std::uint32_t random_sample) noexcept
{
  using namespace std::chrono_literals;
  if (connection_age >= 10s)
    reset();
  const auto jitter_range = static_cast<std::uint64_t>(base_.count() / 5);
  const auto jitter = jitter_range * random_sample / std::numeric_limits<std::uint32_t>::max();
  const auto delay = base_ - std::chrono::milliseconds(jitter);
  base_ = std::min(base_ * 2, 30'000ms);
  return delay;
}
void ObsReconnectBackoff::reset() noexcept
{
  base_ = std::chrono::milliseconds(1000);
}
} // namespace studio_remote
