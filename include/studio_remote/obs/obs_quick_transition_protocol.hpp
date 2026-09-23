#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <studio_remote/ipc/quick_transition_control_mapping.hpp>
#include <studio_remote/obs/obs_hotkey_catalog.hpp>
#include <studio_remote/obs/obs_quick_transition_catalog.hpp>
#include <vector>

namespace studio_remote
{
using ObsQuickTransitionPause = std::function<void(std::chrono::milliseconds)>;
struct ObsQuickTransitionTiming
{
  std::function<std::chrono::steady_clock::time_point()> now;
  ObsQuickTransitionPause pause;
};

/// Windows helper consumer protocol shared by native mapping adapters. Callers serialize their
/// transactions and keep the mapped region alive until the function returns. Required timing
/// callbacks are synchronously borrowed and never retained; the protocol owns no OS resource.
void submit_obs_quick_transition(ipc::QuickTransitionControlMapping& mapping,
                                 ipc::QuickTransitionCommandKind kind,
                                 std::int32_t transition_id,
                                 std::uint32_t duration_ms,
                                 std::chrono::milliseconds timeout,
                                 const ObsQuickTransitionTiming& timing);

/// Copy one stable catalog using the Windows three-attempt sequence check. An incompatible
/// mapping or unstable snapshot is unavailable; a stable empty catalog is an available value.
std::optional<std::vector<ObservedQuickTransition>>
read_obs_quick_transition_catalog(const ipc::QuickTransitionControlMapping& mapping,
                                  const ObsQuickTransitionPause& pause);
std::optional<std::vector<ObservedObsHotkeyBinding>>
read_obs_hotkey_binding_catalog(const ipc::QuickTransitionControlMapping& mapping,
                                const ObsQuickTransitionPause& pause);
} // namespace studio_remote
