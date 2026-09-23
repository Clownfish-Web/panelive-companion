#pragma once

#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/input/hotkey.hpp>
#include <studio_remote/obs/obs_adapter.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace studio_remote
{
struct HotkeyTriggerRequest
{
  std::string request_id;
  std::string hotkey_id;
  std::vector<HotkeyKey> keys;
  HotkeyTarget target = HotkeyTarget::production;
};

enum class HotkeyTriggerStatus
{
  completed,
  already_completed,
  in_progress,
  permission_denied,
  unsupported,
  unavailable
};

struct HotkeyTriggerResult
{
  HotkeyTriggerStatus status = HotkeyTriggerStatus::unavailable;
  std::string reason;
};

class HotkeyService
{
public:
  struct ProductionTarget
  {
    std::string id;
    IObsAdapter* adapter = nullptr;
  };

  HotkeyService(IHotkeyExecutor& system_executor,
                std::vector<ProductionTarget> production_targets,
                AuditStore& audit);

  bool available(const Device& device, HotkeyTarget target) const noexcept;
  HotkeyTriggerResult trigger(const Device& device, const HotkeyTriggerRequest& request);

private:
  using Clock = std::chrono::steady_clock;
  void prune_locked(Clock::time_point now);

  IObsAdapter* production_target(const Device& device) const noexcept;

  IHotkeyExecutor& system_executor_;
  std::vector<ProductionTarget> production_targets_;
  AuditStore& audit_;
  std::mutex mutex_;
  std::unordered_set<std::string> in_progress_;
  std::unordered_map<std::string, Clock::time_point> completed_;
};
} // namespace studio_remote
