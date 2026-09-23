#pragma once

#include "../ipc/macos_ipc_names.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <studio_remote/ipc/quick_transition_control_mapping.hpp>
#include <studio_remote/obs/obs_hotkey_catalog.hpp>
#include <studio_remote/obs/obs_quick_transition_catalog.hpp>

namespace studio_remote::macos
{

/// Calls are serialized per instance; the composition owns the single local command writer.
/// Each call reopens helper-owned memory so a restarted producer is observed independently.
class MacosObsQuickTransitionControl final
{
public:
  explicit MacosObsQuickTransitionControl(
      std::string mapping_name = ipc_names::quick_transition_control);
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
  const std::string mapping_name_;
  mutable std::mutex mutex_;
};

} // namespace studio_remote::macos
