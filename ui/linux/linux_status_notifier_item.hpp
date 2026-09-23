#pragma once

#include <gio/gio.h>
#include <studio_remote/ui/management_interface.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace studio_remote::linux
{
struct LinuxStatusNotifierActions
{
  std::function<void()> open_management;
  std::function<void()> pair_client;
  std::function<void(ManagementInterfaceMode)> set_interface_mode;
  std::function<ManagementInterfaceMode()> interface_mode;
  std::function<void()> shutdown;
};

/// Publishes the desktop status item and its menu through the freedesktop D-Bus protocols.
class LinuxStatusNotifierItem
{
public:
  explicit LinuxStatusNotifierItem(LinuxStatusNotifierActions actions);
  ~LinuxStatusNotifierItem();

  LinuxStatusNotifierItem(const LinuxStatusNotifierItem&) = delete;
  LinuxStatusNotifierItem& operator=(const LinuxStatusNotifierItem&) = delete;

private:
  static void name_acquired(GDBusConnection* connection, const gchar* name, gpointer context);
  static void watcher_owner_changed(GDBusConnection* connection,
                                    const gchar* sender,
                                    const gchar* path,
                                    const gchar* interface_name,
                                    const gchar* signal,
                                    GVariant* parameters,
                                    gpointer context);
  static void method_called(GDBusConnection* connection,
                            const gchar* sender,
                            const gchar* path,
                            const gchar* interface_name,
                            const gchar* method,
                            GVariant* parameters,
                            GDBusMethodInvocation* invocation,
                            gpointer context);
  static GVariant* property_read(GDBusConnection* connection,
                                 const gchar* sender,
                                 const gchar* path,
                                 const gchar* interface_name,
                                 const gchar* property,
                                 GError** error,
                                 gpointer context);

  void register_with_watcher();
  void invoke_menu_item(std::int32_t id);
  void notify_menu_changed();
  GVariant* menu_layout(std::int32_t parent_id, std::int32_t depth) const;
  GVariant* menu_properties(std::int32_t id) const;

  LinuxStatusNotifierActions actions_;
  GDBusConnection* connection_ = nullptr;
  GDBusNodeInfo* status_node_ = nullptr;
  GDBusNodeInfo* menu_node_ = nullptr;
  guint status_registration_ = 0;
  guint menu_registration_ = 0;
  guint name_owner_ = 0;
  guint watcher_subscription_ = 0;
  std::string service_name_;
  std::uint32_t menu_revision_ = 1;
};
} // namespace studio_remote::linux
