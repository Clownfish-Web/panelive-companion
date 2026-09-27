#pragma once

#include <functional>
#include <memory>

@class PLManagementBridge;

namespace studio_remote::macos
{
/// Main-thread frontend resource. Owns notification permission/delivery and hidden-window polling;
/// invalidates callbacks before the borrowed bridge and window are destroyed.
class MacosClientNotifications final
{
public:
  MacosClientNotifications(PLManagementBridge* bridge,
                           std::function<bool()> window_visible,
                           std::function<void()> open_management);
  ~MacosClientNotifications();
  MacosClientNotifications(const MacosClientNotifications&) = delete;
  MacosClientNotifications& operator=(const MacosClientNotifications&) = delete;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
