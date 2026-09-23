#include "webrtc_recovery_coordinator.hpp"

#include <utility>

namespace studio_remote
{
namespace
{
constexpr auto recovery_window = std::chrono::minutes(1);
constexpr auto operation_retention = std::chrono::minutes(10);
constexpr std::size_t recovery_limit = 2;

WebRtcRecoveryOutcome retirement_outcome(const WebRtcRecoveryRequest& request,
                                         const MediaSessionRetirementResult& result)
{
  json body = {{"type", "webrtcSessionRecovery"},
               {"recoveryId", request.recovery_id},
               {"mediaSessionId", request.media_session_id},
               {"replayed", false}};
  switch (result.status)
  {
  case MediaSessionRetirementStatus::retired:
    body["status"] = "accepted";
    body["nextOfferAfterMs"] = 0;
    return {200, std::move(body)};
  case MediaSessionRetirementStatus::no_active_session:
    body["status"] = "no-active-media-session";
    body["reason"] = "no-active-media-session";
    return {409, std::move(body)};
  case MediaSessionRetirementStatus::session_mismatch:
    body["status"] = "session-mismatch";
    body["reason"] = "media-session-replaced";
    body["currentMediaSessionId"] = result.media_session_id;
    return {409, std::move(body)};
  case MediaSessionRetirementStatus::failed:
    body["status"] = "failed";
    body["reason"] = "media-recovery-failed";
    body["cleanupFailures"] = result.cleanup_failures;
    return {500, std::move(body)};
  }
  body["status"] = "failed";
  body["reason"] = "media-recovery-failed";
  return {500, std::move(body)};
}
} // namespace

WebRtcRecoveryCoordinator::WebRtcRecoveryCoordinator(Now now) : now_(std::move(now))
{
}

WebRtcRecoveryOutcome WebRtcRecoveryCoordinator::recover(std::string_view device_session_key,
                                                         const WebRtcRecoveryRequest& request,
                                                         RetirementHandler retire)
{
  const auto now = now_();
  const auto key = operation_key(device_session_key, request.recovery_id);
  {
    std::lock_guard lock(mutex_);
    expire(now);
    if (const auto found = operations_.find(key); found != operations_.end())
    {
      if (!found->second.completed)
        return {409,
                {{"type", "webrtcSessionRecovery"},
                 {"recoveryId", request.recovery_id},
                 {"mediaSessionId", request.media_session_id},
                 {"status", "restart-already-in-progress"},
                 {"retryAfterMs", 250},
                 {"replayed", true}}};
      auto replay = found->second.outcome;
      replay.body["replayed"] = true;
      return replay;
    }
    if (const auto active = active_operations_.find(std::string(device_session_key));
        active != active_operations_.end())
      return {409,
              {{"type", "webrtcSessionRecovery"},
               {"recoveryId", request.recovery_id},
               {"mediaSessionId", request.media_session_id},
               {"status", "restart-already-in-progress"},
               {"activeRecoveryId", active->second},
               {"retryAfterMs", 250},
               {"replayed", false}}};

    auto& attempts = attempts_[std::string(device_session_key)];
    while (!attempts.empty() && now - attempts.front() >= recovery_window)
      attempts.pop_front();
    if (attempts.size() >= recovery_limit)
    {
      const auto retry_after = std::chrono::duration_cast<std::chrono::milliseconds>(
          recovery_window - (now - attempts.front()));
      return {429,
              {{"type", "webrtcSessionRecovery"},
               {"recoveryId", request.recovery_id},
               {"mediaSessionId", request.media_session_id},
               {"status", "rate-limited"},
               {"retryAfterMs", std::max<std::int64_t>(1, retry_after.count())},
               {"replayed", false}}};
    }
    attempts.push_back(now);
    operations_.emplace(key, Operation{now, false, {}});
    active_operations_[std::string(device_session_key)] = request.recovery_id;
  }

  WebRtcRecoveryOutcome outcome;
  try
  {
    outcome = retirement_outcome(request, retire());
  }
  catch (...)
  {
    outcome = {500,
               {{"type", "webrtcSessionRecovery"},
                {"recoveryId", request.recovery_id},
                {"mediaSessionId", request.media_session_id},
                {"status", "failed"},
                {"reason", "media-recovery-failed"},
                {"replayed", false}}};
  }

  {
    std::lock_guard lock(mutex_);
    auto& operation = operations_.at(key);
    operation.completed = true;
    operation.outcome = outcome;
    active_operations_.erase(std::string(device_session_key));
  }
  return outcome;
}

std::string WebRtcRecoveryCoordinator::operation_key(std::string_view device_session_key,
                                                     std::string_view recovery_id)
{
  return std::string(device_session_key) + '\n' + std::string(recovery_id);
}

void WebRtcRecoveryCoordinator::expire(Clock::time_point now)
{
  for (auto iterator = operations_.begin(); iterator != operations_.end();)
    if (iterator->second.completed && now - iterator->second.created >= operation_retention)
      iterator = operations_.erase(iterator);
    else
      ++iterator;
}

} // namespace studio_remote
