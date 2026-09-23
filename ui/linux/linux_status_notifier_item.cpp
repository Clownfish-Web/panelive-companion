#include "linux_status_notifier_item.hpp"

#include <unistd.h>

#include <array>
#include <utility>

namespace studio_remote::linux
{
namespace
{
constexpr char status_path[] = "/StatusNotifierItem";
constexpr char menu_path[] = "/Menu";
constexpr char status_interface[] = "org.kde.StatusNotifierItem";
constexpr char menu_interface[] = "com.canonical.dbusmenu";
constexpr char watcher_name[] = "org.kde.StatusNotifierWatcher";
constexpr char watcher_path[] = "/StatusNotifierWatcher";
constexpr char watcher_interface[] = "org.kde.StatusNotifierWatcher";
constexpr char icon_name[] = "com.clownfishweb.PaneLiveCompanion";

constexpr char status_xml[] = R"xml(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="i" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="OverlayIconName" type="s" access="read"/>
    <property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionMovieName" type="s" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
    <signal name="NewTitle"/><signal name="NewIcon"/><signal name="NewAttentionIcon"/>
    <signal name="NewOverlayIcon"/><signal name="NewToolTip"/>
    <signal name="NewStatus"><arg type="s"/></signal>
  </interface>
</node>)xml";

constexpr char menu_xml[] = R"xml(
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/><arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method>
    <method name="GetGroupProperties"><arg type="ai" direction="in"/><arg type="as" direction="in"/><arg type="a(ia{sv})" direction="out"/></method>
    <method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method>
    <method name="EventGroup"><arg type="a(isvu)" direction="in"/><arg type="ai" direction="out"/></method>
    <method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
    <method name="AboutToShowGroup"><arg type="ai" direction="in"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/></method>
    <signal name="ItemsPropertiesUpdated"><arg type="a(ia{sv})"/><arg type="a(ias)"/></signal>
    <signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal>
  </interface>
</node>)xml";

constexpr std::array<std::int32_t, 7> menu_ids{1, 2, 3, 4, 5, 6, 7};

GVariant* empty_pixmaps()
{
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
  return g_variant_builder_end(&builder);
}

GVariant* empty_int_array()
{
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("ai"));
  return g_variant_builder_end(&builder);
}

void return_error(GDBusMethodInvocation* invocation, std::string_view message)
{
  g_dbus_method_invocation_return_error(
      invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "%s", std::string(message).c_str());
}
} // namespace

LinuxStatusNotifierItem::LinuxStatusNotifierItem(LinuxStatusNotifierActions actions)
    : actions_(std::move(actions)),
      service_name_("org.freedesktop.StatusNotifierItem-" + std::to_string(getpid()) + "-1")
{
  GError* error = nullptr;
  connection_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
  if (!connection_)
  {
    g_clear_error(&error);
    return;
  }
  status_node_ = g_dbus_node_info_new_for_xml(status_xml, &error);
  if (!status_node_)
  {
    g_clear_error(&error);
    return;
  }
  menu_node_ = g_dbus_node_info_new_for_xml(menu_xml, &error);
  if (!menu_node_)
  {
    g_clear_error(&error);
    return;
  }

  static const GDBusInterfaceVTable vtable{method_called, property_read, nullptr, {0}};
  status_registration_ = g_dbus_connection_register_object(
      connection_, status_path, status_node_->interfaces[0], &vtable, this, nullptr, &error);
  if (status_registration_ == 0)
  {
    g_clear_error(&error);
    return;
  }
  menu_registration_ = g_dbus_connection_register_object(
      connection_, menu_path, menu_node_->interfaces[0], &vtable, this, nullptr, &error);
  if (menu_registration_ == 0)
  {
    g_clear_error(&error);
    g_dbus_connection_unregister_object(connection_, status_registration_);
    status_registration_ = 0;
    return;
  }
  watcher_subscription_ = g_dbus_connection_signal_subscribe(connection_,
                                                             "org.freedesktop.DBus",
                                                             "org.freedesktop.DBus",
                                                             "NameOwnerChanged",
                                                             "/org/freedesktop/DBus",
                                                             watcher_name,
                                                             G_DBUS_SIGNAL_FLAGS_NONE,
                                                             watcher_owner_changed,
                                                             this,
                                                             nullptr);
  name_owner_ = g_bus_own_name_on_connection(connection_,
                                             service_name_.c_str(),
                                             G_BUS_NAME_OWNER_FLAGS_NONE,
                                             name_acquired,
                                             nullptr,
                                             this,
                                             nullptr);
}

LinuxStatusNotifierItem::~LinuxStatusNotifierItem()
{
  if (name_owner_ != 0)
    g_bus_unown_name(name_owner_);
  if (watcher_subscription_ != 0)
    g_dbus_connection_signal_unsubscribe(connection_, watcher_subscription_);
  if (menu_registration_ != 0)
    g_dbus_connection_unregister_object(connection_, menu_registration_);
  if (status_registration_ != 0)
    g_dbus_connection_unregister_object(connection_, status_registration_);
  g_clear_pointer(&menu_node_, g_dbus_node_info_unref);
  g_clear_pointer(&status_node_, g_dbus_node_info_unref);
  g_clear_object(&connection_);
}

void LinuxStatusNotifierItem::name_acquired(GDBusConnection*, const gchar*, gpointer context)
{
  static_cast<LinuxStatusNotifierItem*>(context)->register_with_watcher();
}

void LinuxStatusNotifierItem::watcher_owner_changed(GDBusConnection*,
                                                    const gchar*,
                                                    const gchar*,
                                                    const gchar*,
                                                    const gchar*,
                                                    GVariant* parameters,
                                                    gpointer context)
{
  const gchar* name = nullptr;
  const gchar* previous_owner = nullptr;
  const gchar* current_owner = nullptr;
  g_variant_get(parameters, "(&s&s&s)", &name, &previous_owner, &current_owner);
  if (current_owner && *current_owner)
    static_cast<LinuxStatusNotifierItem*>(context)->register_with_watcher();
}

void LinuxStatusNotifierItem::register_with_watcher()
{
  if (!connection_ || name_owner_ == 0)
    return;
  g_dbus_connection_call(connection_,
                         watcher_name,
                         watcher_path,
                         watcher_interface,
                         "RegisterStatusNotifierItem",
                         g_variant_new("(s)", service_name_.c_str()),
                         nullptr,
                         G_DBUS_CALL_FLAGS_NONE,
                         3000,
                         nullptr,
                         nullptr,
                         nullptr);
}

void LinuxStatusNotifierItem::method_called(GDBusConnection*,
                                            const gchar*,
                                            const gchar*,
                                            const gchar* interface_name,
                                            const gchar* method,
                                            GVariant* parameters,
                                            GDBusMethodInvocation* invocation,
                                            gpointer context)
{
  auto& self = *static_cast<LinuxStatusNotifierItem*>(context);
  if (std::string_view(interface_name) == status_interface)
  {
    if (std::string_view(method) == "Activate" || std::string_view(method) == "SecondaryActivate")
      self.actions_.open_management();
    g_dbus_method_invocation_return_value(invocation, nullptr);
    return;
  }
  if (std::string_view(interface_name) != menu_interface)
  {
    return_error(invocation, "Unknown status item interface");
    return;
  }
  if (std::string_view(method) == "GetLayout")
  {
    gint32 parent = 0;
    gint32 depth = -1;
    GVariant* names = nullptr;
    g_variant_get(parameters, "(ii@as)", &parent, &depth, &names);
    g_variant_unref(names);
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(u@(ia{sv}av))", self.menu_revision_, self.menu_layout(parent, depth)));
    return;
  }
  if (std::string_view(method) == "GetGroupProperties")
  {
    GVariant* ids = nullptr;
    GVariant* names = nullptr;
    g_variant_get(parameters, "(@ai@as)", &ids, &names);
    GVariantBuilder rows;
    g_variant_builder_init(&rows, G_VARIANT_TYPE("a(ia{sv})"));
    GVariantIter iterator;
    gint32 id = 0;
    g_variant_iter_init(&iterator, ids);
    while (g_variant_iter_next(&iterator, "i", &id))
      g_variant_builder_add(&rows, "(i@a{sv})", id, self.menu_properties(id));
    g_variant_unref(ids);
    g_variant_unref(names);
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&rows)));
    return;
  }
  if (std::string_view(method) == "Event")
  {
    gint32 id = 0;
    const gchar* event = nullptr;
    GVariant* data = nullptr;
    guint32 timestamp = 0;
    g_variant_get(parameters, "(i&s@vu)", &id, &event, &data, &timestamp);
    if (std::string_view(event) == "clicked")
      self.invoke_menu_item(id);
    g_variant_unref(data);
    g_dbus_method_invocation_return_value(invocation, nullptr);
    return;
  }
  if (std::string_view(method) == "EventGroup")
  {
    GVariant* events = nullptr;
    g_variant_get(parameters, "(@a(isvu))", &events);
    GVariantIter iterator;
    gint32 id = 0;
    const gchar* event = nullptr;
    GVariant* data = nullptr;
    guint32 timestamp = 0;
    g_variant_iter_init(&iterator, events);
    while (g_variant_iter_next(&iterator, "(i&s@vu)", &id, &event, &data, &timestamp))
    {
      if (std::string_view(event) == "clicked")
        self.invoke_menu_item(id);
      g_variant_unref(data);
    }
    g_variant_unref(events);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", empty_int_array()));
    return;
  }
  if (std::string_view(method) == "AboutToShow")
  {
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
    return;
  }
  if (std::string_view(method) == "AboutToShowGroup")
  {
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(@ai@ai)", empty_int_array(), empty_int_array()));
    return;
  }
  return_error(invocation, "Unknown status menu method");
}

GVariant* LinuxStatusNotifierItem::property_read(GDBusConnection*,
                                                 const gchar*,
                                                 const gchar*,
                                                 const gchar* interface_name,
                                                 const gchar* property,
                                                 GError**,
                                                 gpointer)
{
  if (std::string_view(interface_name) == status_interface)
  {
    const std::string_view name(property);
    if (name == "Category")
      return g_variant_new_string("ApplicationStatus");
    if (name == "Id")
      return g_variant_new_string("panelive-companion");
    if (name == "Title")
      return g_variant_new_string("PaneLive Companion");
    if (name == "Status")
      return g_variant_new_string("Active");
    if (name == "WindowId")
      return g_variant_new_int32(0);
    if (name == "IconName" || name == "AttentionIconName")
      return g_variant_new_string(icon_name);
    if (name == "OverlayIconName" || name == "AttentionMovieName")
      return g_variant_new_string("");
    if (name == "IconPixmap" || name == "OverlayIconPixmap" || name == "AttentionIconPixmap")
      return empty_pixmaps();
    if (name == "ToolTip")
      return g_variant_new("(s@a(iiay)ss)",
                           icon_name,
                           empty_pixmaps(),
                           "PaneLive Companion",
                           "Production monitoring and control companion");
    if (name == "ItemIsMenu")
      return g_variant_new_boolean(FALSE);
    if (name == "Menu")
      return g_variant_new_object_path(menu_path);
    return nullptr;
  }
  if (std::string_view(interface_name) == menu_interface)
  {
    const std::string_view name(property);
    if (name == "Version")
      return g_variant_new_uint32(4);
    if (name == "TextDirection")
      return g_variant_new_string("ltr");
    if (name == "Status")
      return g_variant_new_string("normal");
    if (name == "IconThemePath")
      return g_variant_new_strv(nullptr, 0);
  }
  return nullptr;
}

void LinuxStatusNotifierItem::invoke_menu_item(std::int32_t id)
{
  switch (id)
  {
  case 1:
    actions_.open_management();
    break;
  case 2:
    actions_.pair_client();
    break;
  case 4:
    actions_.set_interface_mode(ManagementInterfaceMode::basic);
    notify_menu_changed();
    break;
  case 5:
    actions_.set_interface_mode(ManagementInterfaceMode::advanced);
    notify_menu_changed();
    break;
  case 7:
    actions_.shutdown();
    break;
  default:
    break;
  }
}

void LinuxStatusNotifierItem::notify_menu_changed()
{
  ++menu_revision_;
  g_dbus_connection_emit_signal(connection_,
                                nullptr,
                                menu_path,
                                menu_interface,
                                "LayoutUpdated",
                                g_variant_new("(ui)", menu_revision_, 0),
                                nullptr);
}

GVariant* LinuxStatusNotifierItem::menu_layout(std::int32_t parent_id, std::int32_t depth) const
{
  GVariantBuilder children;
  g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
  if (parent_id == 0 && depth != 0)
  {
    for (const auto id : menu_ids)
      g_variant_builder_add(&children, "v", menu_layout(id, 0));
  }
  return g_variant_new(
      "(i@a{sv}@av)", parent_id, menu_properties(parent_id), g_variant_builder_end(&children));
}

GVariant* LinuxStatusNotifierItem::menu_properties(std::int32_t id) const
{
  GVariantBuilder properties;
  g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
  const auto add_string = [&properties](const char* name, const char* value)
  { g_variant_builder_add(&properties, "{sv}", name, g_variant_new_string(value)); };
  const auto add_bool = [&properties](const char* name, bool value)
  { g_variant_builder_add(&properties, "{sv}", name, g_variant_new_boolean(value)); };
  add_bool("visible", true);
  add_bool("enabled", id != 0 && id != 3 && id != 6);
  switch (id)
  {
  case 0:
    add_string("children-display", "submenu");
    break;
  case 1:
    add_string("label", "Open PaneLive Companion");
    break;
  case 2:
    add_string("label", "Pair new client");
    break;
  case 3:
  case 6:
    add_string("type", "separator");
    break;
  case 4:
  case 5:
    add_string("label", id == 4 ? "Basic interface" : "Advanced interface");
    add_string("toggle-type", "radio");
    g_variant_builder_add(&properties,
                          "{sv}",
                          "toggle-state",
                          g_variant_new_int32(actions_.interface_mode() ==
                                                      (id == 4 ? ManagementInterfaceMode::basic
                                                               : ManagementInterfaceMode::advanced)
                                                  ? 1
                                                  : 0));
    break;
  case 7:
    add_string("label", "Shutdown Companion");
    break;
  default:
    add_bool("visible", false);
    break;
  }
  return g_variant_builder_end(&properties);
}
} // namespace studio_remote::linux
