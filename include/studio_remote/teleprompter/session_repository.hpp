#pragma once

#include <studio_remote/teleprompter/session.hpp>

namespace studio_remote::teleprompter
{
/// In-memory critical sections serialize member authorization, script revisions and commands. Production roles
/// do not grant shared control. Local assignment/revocation methods are never exposed over HTTP.
class ISessionRepository
{
public:
  virtual ~ISessionRepository() = default;
  virtual Result<SessionSnapshot> assign(std::string_view owner_id,
                                         const SessionAssignment& assignment) = 0;
  virtual Result<bool> unassign(std::string_view owner_id,
                                std::string_view session_id,
                                std::uint64_t expected_version) = 0;
  virtual Result<std::vector<SessionSnapshot>> sessions(std::string_view device_id) = 0;
  virtual Result<SessionSnapshot> session(std::string_view device_id,
                                          std::string_view session_id) = 0;
  virtual Result<Script> session_script(std::string_view device_id,
                                        std::string_view session_id,
                                        std::string_view script_id) = 0;
  virtual Result<SessionSnapshot> publish_position(std::string_view device_id,
                                                   std::string_view session_id,
                                                   const SessionPositionReport& report) = 0;
  virtual Result<SessionCommandResult> command(std::string_view device_id,
                                               std::string_view session_id,
                                               const SessionCommand& command) = 0;
};
} // namespace studio_remote::teleprompter
