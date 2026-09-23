#pragma once

#include <studio_remote/teleprompter/runtime_resources.hpp>
#include <studio_remote/teleprompter/session_repository.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <utility>

namespace studio_remote::teleprompter
{
/// Process-lifetime session authority. Resource callbacks run before the state mutex is acquired;
/// immutable compiled scripts are shared with the library. No runtime operation performs I/O.
class MemorySessionRepository final : public ISessionRepository
{
public:
  MemorySessionRepository(SessionDeviceSource devices,
                          ScriptCatalogSource scripts,
                          std::function<std::string()> new_id,
                          std::function<std::uint64_t()> monotonic_ms);
  Result<SessionSnapshot> assign(std::string_view owner,
                                 const SessionAssignment& assignment) override;
  Result<bool>
  unassign(std::string_view owner, std::string_view id, std::uint64_t expected_version) override;
  Result<std::vector<SessionSnapshot>> sessions(std::string_view device) override;
  Result<SessionSnapshot> session(std::string_view device, std::string_view id) override;
  Result<Script>
  session_script(std::string_view device, std::string_view id, std::string_view script_id) override;
  Result<SessionSnapshot> publish_position(std::string_view device,
                                           std::string_view id,
                                           const SessionPositionReport& report) override;
  Result<SessionCommandResult>
  command(std::string_view device, std::string_view id, const SessionCommand& command) override;

private:
  struct RememberedCommand
  {
    std::string device_id;
    std::string command_id;
    std::string identity;
    std::uint64_t accepted_version;
    std::uint64_t received_ms;
  };
  struct Record
  {
    SessionState state;
    std::vector<SessionMember> members;
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>, std::less<>> generations;
    ScriptCatalogSnapshot scripts;
    std::deque<RememberedCommand> commands;
    std::uint64_t observation = 0;
  };
  struct Resources
  {
    std::uint64_t observation;
    ScriptCatalogView scripts;
    SessionDevices devices;
    std::uint64_t now;
  };
  Resources resources();
  bool reconcile(Record& record, const Resources& resources);
  Record* find(std::string_view id, const Resources& resources);
  SessionSnapshot snapshot(const Record& record, SessionRole role, std::uint64_t now) const;
  static std::optional<SessionRole> role(const Record& record, std::string_view device);
  static Result<SessionState>
  change_state(const Record& record, const SessionAction& action, std::uint64_t now);

  SessionDeviceSource devices_;
  ScriptCatalogSource scripts_;
  std::function<std::string()> new_id_;
  std::function<std::uint64_t()> monotonic_ms_;
  std::string clock_id_;
  std::atomic<std::uint64_t> next_observation_{0};
  // Protects records only. Never held while invoking external clocks, IDs or resource callbacks.
  std::mutex mutex_;
  std::map<std::string, Record, std::less<>> records_;
};
} // namespace studio_remote::teleprompter
