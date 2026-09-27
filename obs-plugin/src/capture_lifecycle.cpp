#include "capture_lifecycle.hpp"

#include <utility>

namespace studio_remote::obs_plugin
{

CaptureLifecycle::CaptureLifecycle(StartAction start_action,
                                   StopAction stop_action,
                                   StateHandler state_handler)
    : start_action_(std::move(start_action)), stop_action_(std::move(stop_action)),
      state_handler_(std::move(state_handler))
{
}

void CaptureLifecycle::reconcile(bool demanded, std::uint64_t demand_generation)
{
  if (reload_in_progress_)
    return;

  if (!demanded)
  {
    if (state_ == CompanionCaptureState::active || state_ == CompanionCaptureState::starting)
    {
      set_state(CompanionCaptureState::stopping);
      stop_action_();
    }
    else if (state_ == CompanionCaptureState::faulted)
    {
      stop_action_();
    }
    set_state(CompanionCaptureState::idle);
    return;
  }

  if (state_ == CompanionCaptureState::active || state_ == CompanionCaptureState::starting ||
      (state_ == CompanionCaptureState::faulted && failed_generation_ == demand_generation))
  {
    return;
  }

  set_state(CompanionCaptureState::starting);
  if (start_action_())
  {
    failed_generation_ = 0;
    set_state(CompanionCaptureState::active);
    return;
  }

  stop_action_();
  failed_generation_ = demand_generation;
  set_state(CompanionCaptureState::faulted);
}

void CaptureLifecycle::reconfigure_tracks()
{
  if (reload_in_progress_ || state_ != CompanionCaptureState::active)
    return;
  set_state(CompanionCaptureState::stopping);
  stop_action_();
  set_state(CompanionCaptureState::idle);
}

void CaptureLifecycle::begin_capture_reload()
{
  capture_generation_reset_pending_ = true;
  if (reload_in_progress_)
    return;

  reload_in_progress_ = true;
  if (state_ == CompanionCaptureState::active || state_ == CompanionCaptureState::starting)
  {
    set_state(CompanionCaptureState::stopping);
    stop_action_();
    set_state(CompanionCaptureState::idle);
  }
}

void CaptureLifecycle::complete_capture_reload(bool demanded, std::uint64_t demand_generation)
{
  if (!reload_in_progress_)
    return;

  reload_in_progress_ = false;
  reconcile(demanded, demand_generation);
}

bool CaptureLifecycle::consume_capture_generation_reset() noexcept
{
  const bool pending = capture_generation_reset_pending_;
  capture_generation_reset_pending_ = false;
  return pending;
}

void CaptureLifecycle::shutdown()
{
  reload_in_progress_ = false;
  capture_generation_reset_pending_ = false;
  if (state_ == CompanionCaptureState::active || state_ == CompanionCaptureState::starting)
    set_state(CompanionCaptureState::stopping);
  stop_action_();
  set_state(CompanionCaptureState::idle);
}

CompanionCaptureState CaptureLifecycle::state() const noexcept
{
  return state_;
}

void CaptureLifecycle::set_state(CompanionCaptureState state)
{
  if (state_ == state)
    return;
  state_ = state;
  if (state_handler_)
    state_handler_(state_);
}

} // namespace studio_remote::obs_plugin
