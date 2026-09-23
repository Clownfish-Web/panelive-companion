#pragma once

#include <studio_remote/ui/client_approval_request.hpp>
#include <studio_remote/ui/management_view.hpp>

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace studio_remote
{
class ManagementUiController;
/// Refresh must precede reporting because a successful controller refresh clears its error.
void report_management_approval_error(ManagementUiController& controller, std::string_view message);

struct ManagementApprovalRequesters
{
  std::function<bool(RoleChangeRequest)> role;
  std::function<bool(GlobalSettingChangeRequest)> global_setting;
  /// Call after invalidating the credential. Joins executing decisions, never queued UI work.
  /// Must not be invoked from the device's executing decision callback.
  std::function<void(std::string_view)> cancel_device = {};
};

struct ManagementApprovalDispatchActions
{
  using Task = std::function<void()>;
  /// Nonblocking admission. False means the task was not scheduled. Inline execution is allowed.
  std::function<bool(Task)> post_presentation;
  std::function<bool(Task)> post_decision;
  std::function<bool(ManagementRoleChangeRequest, ManagementApprovalDecision)> present_role;
  std::function<bool(ManagementGlobalSettingChangeRequest, ManagementApprovalDecision)>
      present_global;
  std::function<void(const RoleChangeRequest&, bool)> decide_role;
  std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global;
  /// Native boundary: report/refresh or schedule that operation, containing any reporting error.
  std::function<void(std::string_view)> report_error;
  /// Run on the same serial presentation context as present_role/present_global.
  std::function<void(std::string_view, std::string_view)> cancel_role = {};
  std::function<void(std::string_view, std::string_view)> cancel_global = {};
  std::function<Clock::time_point()> now = Clock::now;
};

/// The Windows frontend's pending-request/dispatch responsibility, shared by native frontends.
/// One lifecycle owner closes before releasing borrowed UI/backend targets. Never close from an
/// action callback. False decisions must be safe on the closing thread (audit/HTTP only).
class ManagementApprovalDispatcher final
{
public:
  static constexpr std::size_t maximum_pending_requests = 32;

  explicit ManagementApprovalDispatcher(ManagementApprovalDispatchActions actions);
  ~ManagementApprovalDispatcher();
  ManagementApprovalDispatcher(const ManagementApprovalDispatcher&) = delete;
  ManagementApprovalDispatcher& operator=(const ManagementApprovalDispatcher&) = delete;

  /// Safe to retain in HTTP callbacks. Weak state cannot prolong frontend/backend lifetimes.
  ManagementApprovalRequesters requesters() const;
  /// Stop admission, finish executing callbacks, deny queued/pending decisions and release targets.
  /// Queued presentation/worker tasks are not waited on and become inert after closure.
  void close();
  /// Retire expired requests and invalidate already queued late decisions.
  void expire_requests();

private:
  class State;
  std::shared_ptr<State> state_;
  std::mutex expiry_mutex_;
  std::condition_variable_any expiry_wake_;
  std::jthread expiry_worker_;
};
} // namespace studio_remote
