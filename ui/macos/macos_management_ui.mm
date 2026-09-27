#include "macos_management_ui.hpp"

#include "macos_client_notifications.hpp"
#include "macos_tray_icon.hpp"
#include "management_approvals.hpp"
#include "management_bridge.hpp"
#include "management_text.hpp"

#import <AppKit/AppKit.h>

// Swift's emitted declarations require AppKit protocols first.
#import "ManagementWindow-Swift.h"

#include <stdexcept>
#include <syslog.h>
#include <utility>

namespace studio_remote::macos
{
class MacosManagementUi::Impl
{
public:
  void initialize(ManagementWorker& worker,
                  const std::string& build_version,
                  std::function<void(const RoleChangeRequest&, bool)> decide_role,
                  std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global)
  {
    if (!NSThread.isMainThread)
      throw std::logic_error("Management UI must be constructed on the main thread");
    bridge_ = make_async_management_bridge(worker);
    window_ = [[PLManagementWindow alloc] initWithBridge:bridge_
                                            buildVersion:native_text(build_version)];
    approvals_ = make_management_approvals(
        window_, worker, std::move(decide_role), std::move(decide_global));
    tray_ = std::make_unique<MacosTrayIcon>(tray_actions());
    __weak PLManagementWindow* window = window_;
    notifications_ = std::make_unique<MacosClientNotifications>(
        bridge_, [window] { return window.isVisible; }, [window] { [window show]; });
  }

  ~Impl()
  {
    notifications_.reset();
    if (approvals_)
      approvals_->close();
    tray_.reset();
    [window_ invalidate];
    [bridge_ invalidate];
  }

  void show_management()
  {
    [window_ show];
  }

  void show_pairing(const ManagementPairing& pairing)
  {
    [window_ showPairingWithPayload:native_text(pairing.payload)
                 expiresUnixSeconds:pairing.expires_unix_seconds];
  }

  void hide_pairing()
  {
    [window_ clearPairing];
  }

  void shutdown()
  {
    [bridge_ shutdown];
  }

  ManagementApprovalRequesters approval_requesters() const
  {
    return approvals_->requesters();
  }

  std::function<void()> pairing_closed_handler() const
  {
    __weak PLManagementWindow* window = window_;
    return [window]
    {
      dispatch_async(dispatch_get_main_queue(), ^{
        try
        {
          [window clearPairing];
        }
        catch (...)
        {
          syslog(LOG_ERR, "Pairing presentation close failed");
        }
      });
    };
  }

private:
  MacosTrayActions tray_actions()
  {
    __weak PLManagementWindow* window = window_;
    __weak PLManagementBridge* bridge = bridge_;
    return {[window] { [window show]; },
            [window] { [window pairClient]; },
            [window](ManagementInterfaceMode mode)
            {
              [window setInterfaceMode:mode == ManagementInterfaceMode::advanced
                                           ? PLManagementModeAdvanced
                                           : PLManagementModeBasic];
            },
            [bridge]
            {
              return bridge.interfaceMode == PLManagementModeAdvanced
                         ? ManagementInterfaceMode::advanced
                         : ManagementInterfaceMode::basic;
            },
            [bridge] { [bridge shutdown]; }};
  }

  PLManagementBridge* bridge_ = nil;
  PLManagementWindow* window_ = nil;
  std::unique_ptr<ManagementApprovalDispatcher> approvals_;
  std::unique_ptr<MacosTrayIcon> tray_;
  std::unique_ptr<MacosClientNotifications> notifications_;
};

MacosManagementUi::MacosManagementUi(
    ManagementWorker& worker,
    const std::string& build_version,
    std::function<void(const RoleChangeRequest&, bool)> decide_role,
    std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global)
    : impl_(std::make_unique<Impl>())
{
  // The completed Impl owns cleanup even when window, approval or tray initialization throws.
  impl_->initialize(worker, build_version, std::move(decide_role), std::move(decide_global));
}

MacosManagementUi::~MacosManagementUi() = default;

void MacosManagementUi::show_management()
{
  impl_->show_management();
}

void MacosManagementUi::show_pairing(const ManagementPairing& pairing)
{
  impl_->show_pairing(pairing);
}

void MacosManagementUi::hide_pairing()
{
  impl_->hide_pairing();
}

void MacosManagementUi::shutdown()
{
  impl_->shutdown();
}

ManagementApprovalRequesters MacosManagementUi::approval_requesters() const
{
  return impl_->approval_requesters();
}

std::function<void()> MacosManagementUi::pairing_closed_handler() const
{
  return impl_->pairing_closed_handler();
}
} // namespace studio_remote::macos
