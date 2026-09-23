#pragma once

#include <studio_remote/ipc/quick_transition_control_mapping.hpp>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace studio_remote::linux
{

struct ObservedObsHotkeyBinding
{
  std::string identifier;
  std::string label;
  std::string key_identifier;
  std::uint32_t modifiers = 0;
};

struct ObservedQuickTransition
{
  std::int32_t transition_id = 0;
  std::uint32_t duration_ms = 0;
  std::string label;
};

class LinuxObsQuickTransitionControl
{
public:
  explicit LinuxObsQuickTransitionControl(
      std::string mapping_name = ipc::quick_transition_control_shared_memory_name);

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

  std::string mapping_name_;
  mutable std::mutex mutex_;
};

} // namespace studio_remote::linux
