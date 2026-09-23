#include "management_approvals.hpp"

#include "management_text.hpp"
#include "management_worker.hpp"

#import <AppKit/AppKit.h>

// The generated Swift header requires AppKit protocol declarations first.
#import "ManagementWindow-Swift.h"

#include <stdexcept>
#include <syslog.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
void deliver_decision(const ManagementApprovalDecision& decision, bool approved) noexcept
{
  try
  {
    decision(approved);
  }
  catch (...)
  {
    syslog(LOG_ERR, "Native approval decision delivery failed");
  }
}
bool present_role(PLManagementWindow* window,
                  const ManagementRoleChangeRequest& request,
                  ManagementApprovalDecision decision)
{
  if (!window)
    return false;
  return static_cast<bool>([window
      requestRoleChangeWithDeviceIdentifier:native_text(request.device_id)
                          requestIdentifier:native_text(request.request_id)
                                 deviceName:native_text(request.device_name)
                              requestedRole:native_text(request.requested_role)
                                backendName:native_text(request.backend_name)
                                   decision:^(BOOL approved) {
                                     deliver_decision(decision, approved);
                                   }]);
}

bool present_global(PLManagementWindow* window,
                    const ManagementGlobalSettingChangeRequest& request,
                    ManagementApprovalDecision decision)
{
  if (!window)
    return false;
  return static_cast<bool>([window
      requestGlobalSettingChangeWithDeviceIdentifier:native_text(request.device_id)
                                   requestIdentifier:native_text(request.request_id)
                                          deviceName:native_text(request.device_name)
                                         settingName:native_text(request.setting_name)
                                             warning:native_text(request.warning)
                                            decision:^(BOOL approved) {
                                              deliver_decision(decision, approved);
                                            }]);
}
} // namespace

std::unique_ptr<ManagementApprovalDispatcher> make_management_approvals(
    PLManagementWindow* window,
    ManagementWorker& worker,
    std::function<void(const RoleChangeRequest&, bool)> decide_role,
    std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global)
{
  if (![NSThread isMainThread] || !window)
    throw std::invalid_argument(
        "Approval presentation requires a native window on the main thread");
  __weak PLManagementWindow* target = window;
  return std::make_unique<ManagementApprovalDispatcher>(ManagementApprovalDispatchActions{
      [](auto action)
      {
        dispatch_async(dispatch_get_main_queue(), ^{
          try
          {
            action();
          }
          catch (...)
          {
            syslog(LOG_ERR, "Native approval presentation dispatch failed");
          }
        });
        return true;
      },
      [&worker](auto action)
      {
        worker.post([action = std::move(action)](ManagementUiController&) { action(); });
        return true;
      },
      [target](ManagementRoleChangeRequest request, ManagementApprovalDecision decision)
      { return present_role(target, request, std::move(decision)); },
      [target](ManagementGlobalSettingChangeRequest request, ManagementApprovalDecision decision)
      { return present_global(target, request, std::move(decision)); },
      std::move(decide_role),
      std::move(decide_global),
      [&worker](std::string_view message) noexcept
      {
        try
        {
          worker.post([message = std::string(message)](ManagementUiController& controller)
                      { report_management_approval_error(controller, message); });
        }
        catch (...)
        {
          syslog(LOG_ERR, "Could not report a client approval failure");
        }
      },
      [target](std::string_view device_id, std::string_view request_id)
      {
        [target cancelRoleChangeWithDeviceIdentifier:native_text(device_id)
                                   requestIdentifier:native_text(request_id)];
      },
      [target](std::string_view device_id, std::string_view request_id)
      {
        [target cancelGlobalSettingChangeWithDeviceIdentifier:native_text(device_id)
                                            requestIdentifier:native_text(request_id)];
      }});
}
} // namespace studio_remote::macos
