#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/events/countdown_event_policy.hpp>

namespace studio_remote
{
inline constexpr std::uint32_t countdown_transition_permission = 1;
inline constexpr std::uint32_t countdown_recording_permission = 2;
inline constexpr std::uint32_t countdown_streaming_permission = 4;
inline constexpr std::size_t maximum_client_event_bytes = 16 * 1024;

std::optional<CountdownEvent> decode_countdown_event(const json& envelope);
ClientEventNotification countdown_notification(const json& envelope, CountdownEventPolicy& policy);
ClientEventNotification rejected_client_event(std::string reason);
} // namespace studio_remote
