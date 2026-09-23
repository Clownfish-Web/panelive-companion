#pragma once

#include <cstdint>

namespace studio_remote
{

inline constexpr std::uint32_t obs_event_general = 1U << 0U;
inline constexpr std::uint32_t obs_event_scenes = 1U << 2U;
inline constexpr std::uint32_t obs_event_inputs = 1U << 3U;
inline constexpr std::uint32_t obs_event_transitions = 1U << 4U;
inline constexpr std::uint32_t obs_event_outputs = 1U << 6U;
inline constexpr std::uint32_t obs_event_scene_items = 1U << 7U;
inline constexpr std::uint32_t obs_event_ui = 1U << 10U;
inline constexpr std::uint32_t obs_event_input_volume_meters = 1U << 16U;
inline constexpr std::uint32_t obs_event_input_active_state_changed = 1U << 17U;

inline constexpr std::uint32_t gateway_obs_event_subscriptions =
    obs_event_general | obs_event_scenes | obs_event_inputs | obs_event_outputs |
    obs_event_transitions | obs_event_scene_items | obs_event_ui | obs_event_input_volume_meters |
    obs_event_input_active_state_changed;

} // namespace studio_remote
