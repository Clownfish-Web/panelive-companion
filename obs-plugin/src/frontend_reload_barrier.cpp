#include "frontend_reload_barrier.hpp"

namespace studio_remote::obs_plugin
{
bool FrontendReloadBarrier::begin() noexcept
{
  return !blocked_.exchange(true, std::memory_order_acq_rel);
}

bool FrontendReloadBarrier::complete() noexcept
{
  return blocked_.exchange(false, std::memory_order_acq_rel);
}

bool FrontendReloadBarrier::blocked() const noexcept
{
  return blocked_.load(std::memory_order_acquire);
}
} // namespace studio_remote::obs_plugin
