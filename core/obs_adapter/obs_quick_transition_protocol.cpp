#include <studio_remote/obs/obs_quick_transition_protocol.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace studio_remote
{
namespace
{
template <std::size_t Size> std::string bounded_string(const std::array<char, Size>& value)
{
  const auto end = std::find(value.begin(), value.end(), '\0');
  return std::string(value.begin(), end);
}
} // namespace

void submit_obs_quick_transition(ipc::QuickTransitionControlMapping& mapping,
                                 ipc::QuickTransitionCommandKind kind,
                                 std::int32_t transition_id,
                                 std::uint32_t duration_ms,
                                 std::chrono::milliseconds timeout,
                                 const ObsQuickTransitionTiming& timing)
{
  if (!ipc::compatible_quick_transition_control(mapping))
    throw std::runtime_error("The OBS quick-transition helper version is incompatible");

  auto sequence = mapping.command_sequence.load(std::memory_order_acquire) + 1;
  // Zero is reserved for idle by the producer, including after uint64 wrap.
  if (sequence == 0)
    sequence = 1;
  mapping.requested_command_kind.store(static_cast<std::int32_t>(kind), std::memory_order_release);
  mapping.requested_transition_id.store(transition_id, std::memory_order_release);
  mapping.requested_duration_ms.store(duration_ms, std::memory_order_release);
  mapping.command_status.store(
      static_cast<std::int32_t>(ipc::QuickTransitionCommandStatus::pending),
      std::memory_order_release);
  mapping.command_sequence.store(sequence, std::memory_order_release);

  const auto deadline = timing.now() + timeout;
  while (mapping.completed_sequence.load(std::memory_order_acquire) != sequence &&
         timing.now() < deadline)
    timing.pause(std::chrono::milliseconds(5));

  const bool completed = mapping.completed_sequence.load(std::memory_order_acquire) == sequence;
  const auto status = static_cast<ipc::QuickTransitionCommandStatus>(
      mapping.command_status.load(std::memory_order_acquire));
  if (!completed)
    throw std::runtime_error("OBS quick-transition helper timed out");
  if (status == ipc::QuickTransitionCommandStatus::duration_control_not_found)
    throw std::runtime_error(
        "OBS quick-transition duration control is unavailable; enable Studio Mode");
  if (status == ipc::QuickTransitionCommandStatus::ui_invocation_failed)
    throw std::runtime_error("OBS rejected the quick-transition UI update");
  if (status != ipc::QuickTransitionCommandStatus::completed)
    throw std::runtime_error("OBS quick-transition button is no longer available");
}

std::optional<std::vector<ObservedQuickTransition>>
read_obs_quick_transition_catalog(const ipc::QuickTransitionControlMapping& mapping,
                                  const ObsQuickTransitionPause& pause)
{
  if (!ipc::compatible_quick_transition_control(mapping))
    return std::nullopt;

  for (int attempt = 0; attempt < 3; ++attempt)
  {
    const auto first_sequence = mapping.catalog_sequence.load(std::memory_order_acquire);
    if (first_sequence % 2U != 0)
    {
      pause(std::chrono::milliseconds(1));
      continue;
    }

    const auto count =
        std::min<std::uint32_t>(mapping.catalog_count.load(std::memory_order_acquire),
                                ipc::max_quick_transition_catalog_entries);
    std::vector<ObservedQuickTransition> entries;
    entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const auto& source = mapping.catalog[index];
      if (source.transition_id <= 0)
        continue;
      entries.push_back({source.transition_id, source.duration_ms, bounded_string(source.label)});
    }

    const auto second_sequence = mapping.catalog_sequence.load(std::memory_order_acquire);
    if (first_sequence == second_sequence && second_sequence % 2U == 0)
      return entries;
  }

  return std::nullopt;
}

std::optional<std::vector<ObservedObsHotkeyBinding>>
read_obs_hotkey_binding_catalog(const ipc::QuickTransitionControlMapping& mapping,
                                const ObsQuickTransitionPause& pause)
{
  if (!ipc::compatible_quick_transition_control(mapping))
    return std::nullopt;

  for (int attempt = 0; attempt < 3; ++attempt)
  {
    const auto first_sequence = mapping.hotkey_catalog_sequence.load(std::memory_order_acquire);
    if (first_sequence % 2U != 0)
    {
      pause(std::chrono::milliseconds(1));
      continue;
    }

    const auto count =
        std::min<std::uint32_t>(mapping.hotkey_catalog_count.load(std::memory_order_acquire),
                                ipc::max_obs_hotkey_binding_entries);
    std::vector<ObservedObsHotkeyBinding> entries;
    entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const auto& source = mapping.hotkey_catalog[index];
      const auto identifier = bounded_string(source.identifier);
      const auto label = bounded_string(source.label);
      const auto key_identifier = bounded_string(source.key_identifier);
      if (identifier.empty() || label.empty() || key_identifier.empty())
        continue;
      entries.push_back({identifier, label, key_identifier, source.modifiers});
    }

    const auto second_sequence = mapping.hotkey_catalog_sequence.load(std::memory_order_acquire);
    if (first_sequence == second_sequence && second_sequence % 2U == 0)
      return entries;
  }

  return std::nullopt;
}
} // namespace studio_remote
