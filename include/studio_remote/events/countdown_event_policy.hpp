#pragma once

#include <studio_remote/events/client_event_relay.hpp>
#include <studio_remote/events/countdown_event.hpp>

#include <chrono>
#include <map>

#include <string>
#include <tuple>

namespace studio_remote
{
enum class CountdownAdmission
{
  accepted,
  duplicate,
  unauthorized,
  unknown_countdown,
  context_mismatch,
  terminal,
  expired,
  rate_limited,
  capacity_exceeded
};

/// Relay-thread-owned deterministic policy; callers supply monotonic time and current authority.
/// Identity tombstones survive socket replacement for ten minutes; active ownership does not.
class CountdownEventPolicy
{
public:
  using Clock = std::chrono::steady_clock;
  CountdownAdmission accept(const ClientEventOrigin& origin,
                            const CountdownEvent& event,
                            CountdownPermissions permissions,
                            Clock::time_point now);
  void disconnect(std::uint64_t connection_id);
  void expire(Clock::time_point now);

private:
  struct Lifecycle
  {
    CountdownEvent started;
    std::optional<CountdownEvent> terminal;
    std::uint64_t connection_id;
    Clock::time_point accepted_at;
  };
  using Identity = std::tuple<std::string, std::string, std::string>;

  std::map<Identity, Lifecycle> lifecycles_;
};
} // namespace studio_remote
