#include "hotkey_service.hpp"

#include <algorithm>
#include <iterator>

namespace studio_remote
{
namespace
{
constexpr auto request_lifetime = std::chrono::minutes(10);
constexpr std::size_t maximum_completed_requests = 1024;
} // namespace

HotkeyService::HotkeyService(IHotkeyExecutor& system_executor,
                             std::vector<ProductionTarget> production_targets,
                             AuditStore& audit)
    : system_executor_(system_executor), production_targets_(std::move(production_targets)),
      audit_(audit)
{
}

IObsAdapter* HotkeyService::production_target(const Device& device) const noexcept
{
  const auto target = std::find_if(production_targets_.begin(),
                                   production_targets_.end(),
                                   [&](const ProductionTarget& candidate)
                                   { return candidate.id == device.production_provider; });
  return target == production_targets_.end() ? nullptr : target->adapter;
}

bool HotkeyService::available(const Device& device, HotkeyTarget target) const noexcept
{
  if (target == HotkeyTarget::system)
  {
    const auto* adapter = production_target(device);
    return (!adapter || adapter->backend_info().capabilities.system_hotkeys) &&
           system_executor_.available();
  }
  const auto* adapter = production_target(device);
  return adapter && adapter->connected();
}

HotkeyTriggerResult HotkeyService::trigger(const Device& device,
                                           const HotkeyTriggerRequest& request)
{
  if (device.role < Role::operator_)
    return {HotkeyTriggerStatus::permission_denied, "operator-role-required"};

  const auto cache_key = device.id + ":" + request.request_id;
  {
    std::lock_guard lock(mutex_);
    prune_locked(Clock::now());
    if (completed_.contains(cache_key))
      return {HotkeyTriggerStatus::already_completed, {}};
    if (in_progress_.contains(cache_key))
      return {HotkeyTriggerStatus::in_progress, {}};
  }
  if (!available(device, request.target))
    return {HotkeyTriggerStatus::unavailable,
            request.target == HotkeyTarget::system ? "interactive-desktop-unavailable"
                                                   : "production-backend-unavailable"};
  {
    std::lock_guard lock(mutex_);
    if (completed_.contains(cache_key))
      return {HotkeyTriggerStatus::already_completed, {}};
    if (in_progress_.contains(cache_key))
      return {HotkeyTriggerStatus::in_progress, {}};
    in_progress_.insert(cache_key);
  }

  try
  {
    if (request.target == HotkeyTarget::system)
      system_executor_.trigger(request.keys);
    else
      production_target(device)->trigger_hotkey({request.hotkey_id, request.keys});
  }
  catch (const HotkeyExecutionError& error)
  {
    {
      std::lock_guard lock(mutex_);
      in_progress_.erase(cache_key);
    }
    audit_.command(device.id, "trigger-hotkey", "pc-hotkey", "failed", request.request_id);
    return {HotkeyTriggerStatus::unsupported, error.reason()};
  }
  catch (...)
  {
    {
      std::lock_guard lock(mutex_);
      in_progress_.erase(cache_key);
    }
    audit_.command(device.id, "trigger-hotkey", "pc-hotkey", "failed", request.request_id);
    return {HotkeyTriggerStatus::unavailable,
            request.target == HotkeyTarget::system ? "input-injection-failed"
                                                   : "production-hotkey-trigger-failed"};
  }

  {
    std::lock_guard lock(mutex_);
    in_progress_.erase(cache_key);
    completed_.insert_or_assign(cache_key, Clock::now());
    prune_locked(Clock::now());
  }
  audit_.command(device.id, "trigger-hotkey", "pc-hotkey", "completed", request.request_id);
  return {HotkeyTriggerStatus::completed, {}};
}

void HotkeyService::prune_locked(Clock::time_point now)
{
  for (auto iterator = completed_.begin(); iterator != completed_.end();)
  {
    if (now - iterator->second > request_lifetime)
      iterator = completed_.erase(iterator);
    else
      ++iterator;
  }
  while (completed_.size() > maximum_completed_requests)
  {
    auto oldest = completed_.begin();
    for (auto iterator = std::next(completed_.begin()); iterator != completed_.end(); ++iterator)
      if (iterator->second < oldest->second)
        oldest = iterator;
    completed_.erase(oldest);
  }
}
} // namespace studio_remote
