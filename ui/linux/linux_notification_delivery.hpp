#pragma once

#include <studio_remote/ui/management_client_notifications.hpp>

#include <gio/gio.h>

#include <memory>

namespace studio_remote::linux
{
/// GTK-thread-owned transient desktop delivery; independent of gateway lifetime.
class LinuxNotificationDelivery
{
public:
  explicit LinuxNotificationDelivery(GApplication* application);
  ~LinuxNotificationDelivery();
  LinuxNotificationDelivery(const LinuxNotificationDelivery&) = delete;
  LinuxNotificationDelivery& operator=(const LinuxNotificationDelivery&) = delete;

  void show(const ManagementClientNotification& notification);

private:
  struct State;
  // Pending D-Bus replies retain delivery state, never the frontend or its window.
  std::shared_ptr<State> state_;
  guint subscription_ = 0;
  guint watcher_ = 0;
};
} // namespace studio_remote::linux
