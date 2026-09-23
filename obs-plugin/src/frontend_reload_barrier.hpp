#pragma once

#include <atomic>

namespace studio_remote::obs_plugin
{
// Blocks OBS-owned source attachment while a collection or profile is being destroyed and loaded.
class FrontendReloadBarrier
{
public:
  bool begin() noexcept;
  bool complete() noexcept;
  bool blocked() const noexcept;

private:
  std::atomic<bool> blocked_{false};
};
} // namespace studio_remote::obs_plugin
