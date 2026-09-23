#pragma once

#include "teleprompter_protocol.hpp"

namespace studio_remote
{
[[nodiscard]] bool teleprompter_session_read_path(std::string_view path);
[[nodiscard]] bool teleprompter_session_command_path(std::string_view path);
[[nodiscard]] TeleprompterResponse
teleprompter_session_read(teleprompter::ISessionRepository* repository,
                          std::string_view device_id,
                          std::string_view path);
[[nodiscard]] TeleprompterResponse
teleprompter_session_command(teleprompter::ISessionRepository* repository,
                             std::string_view device_id,
                             std::string_view path,
                             const nlohmann::json& body);
[[nodiscard]] bool teleprompter_session_position_path(std::string_view path);
[[nodiscard]] TeleprompterResponse
teleprompter_session_position(teleprompter::ISessionRepository* repository,
                              std::string_view device_id,
                              std::string_view path,
                              const nlohmann::json& body);
[[nodiscard]] nlohmann::json
teleprompter_session_event(const teleprompter::SessionSnapshot& snapshot);
[[nodiscard]] nlohmann::json teleprompter_session_capability();
} // namespace studio_remote
