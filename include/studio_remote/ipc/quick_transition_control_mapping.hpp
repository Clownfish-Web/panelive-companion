#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace studio_remote::ipc
{

inline constexpr wchar_t quick_transition_control_mapping_name[] =
    L"Local\\panelive-companion.quick-transition-control.v1";
inline constexpr char quick_transition_control_shared_memory_name[] =
    "/clownfishweb-panelive-quick-transition-control-v1";
inline constexpr std::uint32_t quick_transition_control_magic = 0x51544354;
inline constexpr std::uint32_t quick_transition_control_version = 4;
inline constexpr std::uint32_t max_quick_transition_catalog_entries = 32;
inline constexpr std::uint32_t max_quick_transition_catalog_label = 128;
inline constexpr std::uint32_t max_obs_hotkey_binding_entries = 512;
inline constexpr std::uint32_t max_obs_hotkey_identifier = 256;
inline constexpr std::uint32_t max_obs_hotkey_label = 256;
inline constexpr std::uint32_t max_obs_hotkey_key_identifier = 64;

inline constexpr std::uint32_t obs_hotkey_modifier_shift = 1U << 0U;
inline constexpr std::uint32_t obs_hotkey_modifier_control = 1U << 1U;
inline constexpr std::uint32_t obs_hotkey_modifier_alt = 1U << 2U;
inline constexpr std::uint32_t obs_hotkey_modifier_command = 1U << 3U;

enum class QuickTransitionCommandStatus : std::int32_t
{
  pending = 0,
  completed = 1,
  transition_not_found = -1,
  duration_control_not_found = -2,
  ui_invocation_failed = -3,
};

enum class QuickTransitionCommandKind : std::int32_t
{
  trigger = 1,
  set_duration = 2,
};

struct QuickTransitionCatalogEntry
{
  std::int32_t transition_id = 0;
  std::uint32_t duration_ms = 0;
  std::array<char, max_quick_transition_catalog_label> label{};
};

struct ObsHotkeyBindingEntry
{
  std::array<char, max_obs_hotkey_identifier> identifier{};
  std::array<char, max_obs_hotkey_label> label{};
  std::array<char, max_obs_hotkey_key_identifier> key_identifier{};
  std::uint32_t modifiers = 0;
};

struct QuickTransitionControlMapping
{
  std::uint32_t magic = quick_transition_control_magic;
  std::uint32_t version = quick_transition_control_version;
  std::uint32_t structure_size = 0;
  std::uint32_t producer_process_id = 0;
  alignas(8) std::atomic<std::uint64_t> command_sequence{0};
  alignas(4) std::atomic<std::int32_t> requested_command_kind{
      static_cast<std::int32_t>(QuickTransitionCommandKind::trigger)};
  alignas(4) std::atomic<std::int32_t> requested_transition_id{0};
  alignas(4) std::atomic<std::uint32_t> requested_duration_ms{0};
  alignas(4) std::atomic<std::int32_t> command_status{
      static_cast<std::int32_t>(QuickTransitionCommandStatus::pending)};
  alignas(8) std::atomic<std::uint64_t> completed_sequence{0};
  alignas(8) std::atomic<std::uint64_t> catalog_sequence{0};
  alignas(4) std::atomic<std::uint32_t> catalog_count{0};
  std::array<QuickTransitionCatalogEntry, max_quick_transition_catalog_entries> catalog{};
  alignas(8) std::atomic<std::uint64_t> hotkey_catalog_sequence{0};
  alignas(4) std::atomic<std::uint32_t> hotkey_catalog_count{0};
  std::array<ObsHotkeyBindingEntry, max_obs_hotkey_binding_entries> hotkey_catalog{};
};

static_assert(std::atomic<std::int32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

inline bool compatible_quick_transition_control(const QuickTransitionControlMapping& mapping)
{
  return mapping.magic == quick_transition_control_magic &&
         mapping.version == quick_transition_control_version &&
         mapping.structure_size == sizeof(QuickTransitionControlMapping);
}

} // namespace studio_remote::ipc
