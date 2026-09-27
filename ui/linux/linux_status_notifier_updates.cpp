#include "linux_status_notifier_item.hpp"

#include <utility>

namespace studio_remote::linux
{
void LinuxStatusNotifierItem::update(LinuxDesktopStatus status)
{
  if (status == status_)
    return;
  const bool attention_changed = status.attention != status_.attention;
  status_ = std::move(status);
  if (!connection_ || status_registration_ == 0)
    return;
  const auto emit = [this](const char* signal, GVariant* parameters)
  {
    GError* error = nullptr;
    if (!g_dbus_connection_emit_signal(connection_,
                                       nullptr,
                                       "/StatusNotifierItem",
                                       "org.kde.StatusNotifierItem",
                                       signal,
                                       parameters,
                                       &error))
    {
      g_warning("Could not publish Companion desktop status: %s", error->message);
      g_clear_error(&error);
    }
  };
  emit("NewTitle", nullptr);
  emit("NewToolTip", nullptr);
  if (attention_changed)
    emit("NewStatus", g_variant_new("(s)", status_.attention ? "NeedsAttention" : "Active"));
  notify_menu_changed();
}
} // namespace studio_remote::linux
