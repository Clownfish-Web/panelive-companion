#pragma once

#include <studio_remote/obs/obs_hotkey_catalog.hpp>
#include <studio_remote/obs/obs_quick_transition_catalog.hpp>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <studio_remote/ipc/quick_transition_control_mapping.hpp>

namespace studio_remote::windows
{

class WindowsObsQuickTransitionControl
{
public:
  void trigger(std::int32_t transition_id,
               std::chrono::milliseconds timeout = std::chrono::seconds(2)) const;
  void configure_duration(std::int32_t transition_id,
                          std::uint32_t duration_ms,
                          std::chrono::milliseconds timeout = std::chrono::seconds(2)) const;
  std::optional<std::vector<ObservedQuickTransition>> catalog() const;
  std::optional<std::vector<ObservedObsHotkeyBinding>> hotkey_catalog() const;

private:
  void submit(ipc::QuickTransitionCommandKind kind,
              std::int32_t transition_id,
              std::uint32_t duration_ms,
              std::chrono::milliseconds timeout) const;
  mutable std::mutex mutex_;
};

} // namespace studio_remote::windows
