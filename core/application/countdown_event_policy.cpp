#include <studio_remote/events/countdown_event_policy.hpp>

#include <algorithm>

namespace studio_remote
{
namespace
{
bool permitted(const CountdownEvent& event, CountdownPermissions permissions)
{
  if (event.operation == CountdownOperation::studio_transition)
    return permissions.transition;
  return (!event.recording || permissions.recording) && (!event.streaming || permissions.streaming);
}

bool same_context(const CountdownEvent& left, const CountdownEvent& right)
{
  return left.operation == right.operation && left.duration_seconds == right.duration_seconds &&
         left.recording == right.recording && left.streaming == right.streaming &&
         left.context == right.context;
}
} // namespace

CountdownAdmission CountdownEventPolicy::accept(const ClientEventOrigin& origin,
                                                const CountdownEvent& event,
                                                CountdownPermissions permissions,
                                                Clock::time_point now)
{

  const Identity identity{origin.adapter_scope_id, origin.device_id, event.countdown_id};
  const auto found = lifecycles_.find(identity);
  if (found != lifecycles_.end())
  {
    auto& lifecycle = found->second;
    if (event == lifecycle.started || (lifecycle.terminal && event == *lifecycle.terminal))
      return CountdownAdmission::duplicate;
    if (!same_context(event, lifecycle.started))
      return CountdownAdmission::context_mismatch;
    if (lifecycle.connection_id != origin.connection_id ||
        now - lifecycle.accepted_at > std::chrono::seconds(30))
      return CountdownAdmission::expired;
    if (lifecycle.terminal || event.stage == CountdownStage::started)
      return CountdownAdmission::terminal;
    if (event.stage != CountdownStage::cancelled && !permitted(event, permissions))
      return CountdownAdmission::unauthorized;
    lifecycle.terminal = event;
    return CountdownAdmission::accepted;
  }
  if (event.stage != CountdownStage::started)
    return CountdownAdmission::unknown_countdown;
  if (!permitted(event, permissions))
    return CountdownAdmission::unauthorized;
  const auto recent =
      std::count_if(lifecycles_.begin(),
                    lifecycles_.end(),
                    [&](const auto& entry)
                    {
                      return std::get<1>(entry.first) == origin.device_id &&
                             now - entry.second.accepted_at < std::chrono::minutes(1);
                    });
  if (recent >= 20)
    return CountdownAdmission::rate_limited;
  if (lifecycles_.size() >= 4096)
    return CountdownAdmission::capacity_exceeded;
  lifecycles_.emplace(identity, Lifecycle{event, std::nullopt, origin.connection_id, now});
  return CountdownAdmission::accepted;
}

void CountdownEventPolicy::disconnect(std::uint64_t connection_id)
{

  for (auto& [identity, lifecycle] : lifecycles_)
  {
    static_cast<void>(identity);
    if (lifecycle.connection_id == connection_id)
      lifecycle.connection_id = 0;
  }
}

void CountdownEventPolicy::expire(Clock::time_point now)
{

  std::erase_if(lifecycles_,
                [&](const auto& entry)
                { return now - entry.second.accepted_at >= std::chrono::minutes(10); });
}
} // namespace studio_remote
