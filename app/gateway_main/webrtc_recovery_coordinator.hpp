#pragma once

#include "../../core/transport/libdatachannel_transport.hpp"
#include "webrtc_recovery_protocol.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace studio_remote
{
struct WebRtcRecoveryOutcome
{
  int http_status = 500;
  json body;
};

class WebRtcRecoveryCoordinator
{
public:
  using Clock = std::chrono::steady_clock;
  using RetirementHandler = std::function<MediaSessionRetirementResult()>;
  using Now = std::function<Clock::time_point()>;

  explicit WebRtcRecoveryCoordinator(Now now = Clock::now);

  WebRtcRecoveryOutcome recover(std::string_view device_session_key,
                                const WebRtcRecoveryRequest& request,
                                RetirementHandler retire);

private:
  struct Operation
  {
    Clock::time_point created;
    bool completed = false;
    WebRtcRecoveryOutcome outcome;
  };

  static std::string operation_key(std::string_view device_session_key,
                                   std::string_view recovery_id);
  void expire(Clock::time_point now);

  Now now_;
  std::mutex mutex_;
  std::unordered_map<std::string, Operation> operations_;
  std::unordered_map<std::string, std::string> active_operations_;
  std::unordered_map<std::string, std::deque<Clock::time_point>> attempts_;
};

} // namespace studio_remote
