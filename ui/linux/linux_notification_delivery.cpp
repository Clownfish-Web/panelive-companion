#include "linux_notification_delivery.hpp"

namespace studio_remote::linux
{
namespace
{
constexpr auto service = "org.freedesktop.Notifications";
constexpr auto path = "/org/freedesktop/Notifications";
constexpr auto desktop_id = "com.clownfishweb.PaneLiveCompanion";
constexpr int call_timeout_ms = 2000;

template <typename T> using Object = std::unique_ptr<T, decltype(&g_object_unref)>;

void close_notification(GDBusConnection* connection, const char* owner, guint id)
{
  if (!id || !*owner)
    return;
  g_dbus_connection_call(
      connection,
      owner,
      path,
      service,
      "CloseNotification",
      g_variant_new("(u)", id),
      G_VARIANT_TYPE_UNIT,
      G_DBUS_CALL_FLAGS_NONE,
      call_timeout_ms,
      nullptr,
      [](GObject* source, GAsyncResult* result, gpointer)
      {
        GError* error = nullptr;
        auto* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
        if (reply)
          g_variant_unref(reply);
        else
          g_warning("Could not close desktop notification: %s", error->message);
        g_clear_error(&error);
      },
      nullptr);
}
} // namespace

struct LinuxNotificationDelivery::State : std::enable_shared_from_this<State>
{
  explicit State(GApplication* app)
      : application(G_APPLICATION(g_object_ref(app)), g_object_unref),
        connection(G_DBUS_CONNECTION(g_object_ref(g_application_get_dbus_connection(app))),
                   g_object_unref)
  {
  }

  Object<GApplication> application;
  Object<GDBusConnection> connection;
  std::string owner;
  std::optional<ManagementClientNotification> pending;
  guint notification_id = 0;
  bool sending = false;
  bool stopped = false;

  struct Request
  {
    std::shared_ptr<State> state;
    std::string owner;
  };

  void send()
  {
    if (stopped || sending || !pending || owner.empty())
      return;
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&hints, "{sv}", "transient", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(desktop_id));
    const char* actions[] = {"default", "Open Companion", nullptr};
    // Bodies may contain client-supplied names; the notification protocol permits markup.
    const std::unique_ptr<gchar, decltype(&g_free)> body(
        g_markup_escape_text(pending->body.c_str(), -1), g_free);
    auto* parameters = g_variant_new("(susss^asa{sv}i)",
                                     "PaneLive Companion",
                                     notification_id,
                                     desktop_id,
                                     pending->title.c_str(),
                                     body.get(),
                                     actions,
                                     &hints,
                                     -1);
    pending.reset();
    sending = true;
    g_dbus_connection_call(connection.get(),
                           owner.c_str(),
                           path,
                           service,
                           "Notify",
                           parameters,
                           G_VARIANT_TYPE("(u)"),
                           G_DBUS_CALL_FLAGS_NONE,
                           call_timeout_ms,
                           nullptr,
                           sent,
                           new Request{shared_from_this(), owner});
  }

  static void sent(GObject* source, GAsyncResult* result, gpointer data) noexcept
  {
    const std::unique_ptr<Request> request(static_cast<Request*>(data));
    auto& state = *request->state;
    GError* error = nullptr;
    auto* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    guint id = 0;
    if (reply)
    {
      g_variant_get(reply, "(u)", &id);
      g_variant_unref(reply);
    }
    else
      g_warning("Could not deliver desktop notification: %s", error->message);
    g_clear_error(&error);
    state.sending = false;
    if (state.stopped || state.owner != request->owner)
      close_notification(state.connection.get(), request->owner.c_str(), id);
    else
      state.notification_id = id;
    try
    {
      state.send();
    }
    catch (...)
    {
      g_warning("Could not deliver queued desktop notification");
    }
  }

  static void signal(GDBusConnection*,
                     const gchar*,
                     const gchar*,
                     const gchar*,
                     const gchar* name,
                     GVariant* parameters,
                     gpointer data) noexcept
  {
    auto& state = *static_cast<State*>(data);
    if (state.stopped)
      return;
    if (g_str_equal(name, "NotificationClosed") &&
        g_variant_is_of_type(parameters, G_VARIANT_TYPE("(uu)")))
    {
      guint id = 0;
      guint reason = 0;
      g_variant_get(parameters, "(uu)", &id, &reason);
      if (id == state.notification_id)
        state.notification_id = 0;
    }
    else if (g_str_equal(name, "ActionInvoked") &&
             g_variant_is_of_type(parameters, G_VARIANT_TYPE("(us)")))
    {
      guint id = 0;
      const char* action = nullptr;
      g_variant_get(parameters, "(u&s)", &id, &action);
      if (id == state.notification_id && g_str_equal(action, "default"))
        g_action_group_activate_action(
            G_ACTION_GROUP(state.application.get()), "open-management", nullptr);
    }
  }
};

LinuxNotificationDelivery::LinuxNotificationDelivery(GApplication* application)
    : state_(std::make_shared<State>(application))
{
  subscription_ = g_dbus_connection_signal_subscribe(state_->connection.get(),
                                                     service,
                                                     service,
                                                     nullptr,
                                                     path,
                                                     nullptr,
                                                     G_DBUS_SIGNAL_FLAGS_NONE,
                                                     State::signal,
                                                     state_.get(),
                                                     nullptr);
  watcher_ = g_bus_watch_name_on_connection(
      state_->connection.get(),
      service,
      G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
      [](GDBusConnection*, const gchar*, const gchar* owner, gpointer data)
      {
        auto& state = *static_cast<State*>(data);
        try
        {
          state.owner = owner;
          state.send();
        }
        catch (...)
        {
          g_warning("Could not initialize desktop notification delivery");
        }
      },
      [](GDBusConnection*, const gchar*, gpointer data)
      {
        auto& state = *static_cast<State*>(data);
        state.owner.clear();
        state.notification_id = 0;
        state.pending.reset();
      },
      state_.get(),
      nullptr);
}

LinuxNotificationDelivery::~LinuxNotificationDelivery()
{
  state_->stopped = true;
  state_->pending.reset();
  g_bus_unwatch_name(watcher_);
  g_dbus_connection_signal_unsubscribe(state_->connection.get(), subscription_);
  close_notification(state_->connection.get(), state_->owner.c_str(), state_->notification_id);
}

void LinuxNotificationDelivery::show(const ManagementClientNotification& notification)
{
  state_->pending = notification;
  state_->send();
}
} // namespace studio_remote::linux
