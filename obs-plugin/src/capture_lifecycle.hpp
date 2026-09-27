#pragma once

#include "companion_capture_status.hpp"

#include <cstdint>
#include <functional>

namespace studio_remote::obs_plugin
{

class CaptureLifecycle
{
public:
  using StartAction = std::function<bool()>;
  using StopAction = std::function<void()>;
  using StateHandler = std::function<void(CompanionCaptureState)>;

  CaptureLifecycle(StartAction start_action, StopAction stop_action, StateHandler state_handler);

  void reconcile(bool demanded, std::uint64_t demand_generation);
  // Track demand changes stop/recreate capture without invalidating client media sessions.
  void reconfigure_tracks();
  void begin_capture_reload();
  void complete_capture_reload(bool demanded, std::uint64_t demand_generation);
  bool consume_capture_generation_reset() noexcept;
  void shutdown();
  CompanionCaptureState state() const noexcept;

private:
  void set_state(CompanionCaptureState state);

  StartAction start_action_;
  StopAction stop_action_;
  StateHandler state_handler_;
  CompanionCaptureState state_ = CompanionCaptureState::idle;
  std::uint64_t failed_generation_ = 0;
  bool reload_in_progress_ = false;
  bool capture_generation_reset_pending_ = false;
};

} // namespace studio_remote::obs_plugin
