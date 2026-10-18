#include "linux_ui_helpers.hpp"

namespace studio_remote::linux::ui
{
namespace
{
void restore_desktop_environment(GAppLaunchContext* context)
{
  if (!g_getenv("PANELIVE_LINUX_LAUNCHER"))
    return;
  // Match the variables changed by packaging/linux/launch.sh. Change only the
  // child environment: GTK and worker threads still need our bundled runtime.
  for (const auto* variable : {"LD_LIBRARY_PATH", "GSETTINGS_SCHEMA_DIR", "GIO_MODULE_DIR",
                              "GIO_EXTRA_MODULES", "GDK_PIXBUF_MODULEDIR", "XDG_DATA_DIRS",
                              "FONTCONFIG_FILE", "GSK_RENDERER", "GTK_EXE_PREFIX",
                              "GTK_DATA_PREFIX", "GTK_MODULES", "GTK_PATH", "GTK_IM_MODULE",
                              "GDK_PIXBUF_MODULE_FILE"})
  {
    const auto saved = std::string("PANELIVE_HOST_") + variable;
    if (const auto* value = g_getenv(saved.c_str()))
      g_app_launch_context_setenv(context, variable, value);
    else
      g_app_launch_context_unsetenv(context, variable);
    g_app_launch_context_unsetenv(context, saved.c_str());
  }
  g_app_launch_context_unsetenv(context, "PANELIVE_LINUX_LAUNCHER");
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void link_opened(GObject*, GAsyncResult* result, gpointer data)
{
  auto* button = GTK_LINK_BUTTON(data);
  GError* error = nullptr;
  if (g_app_info_launch_default_for_uri_finish(result, &error))
    gtk_link_button_set_visited(button, TRUE);
  else
  {
    g_warning("Could not open link: %s", error->message);
    auto* root = gtk_widget_get_root(GTK_WIDGET(button));
    if (GTK_IS_WINDOW(root))
    {
      auto* dialog = gtk_message_dialog_new(GTK_WINDOW(root), GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                            "%s", "Could not open your browser");
      gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s\n\n%s",
                                                gtk_link_button_get_uri(button), error->message);
      g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* dialog, int, gpointer)
                                                       { gtk_window_destroy(GTK_WINDOW(dialog)); }),
                       nullptr);
      gtk_window_present(GTK_WINDOW(dialog));
    }
    g_error_free(error);
  }
  g_object_unref(button);
}
G_GNUC_END_IGNORE_DEPRECATIONS

gboolean activate_link(GtkLinkButton* button, gpointer)
{
  auto* context = gdk_display_get_app_launch_context(gtk_widget_get_display(GTK_WIDGET(button)));
  restore_desktop_environment(G_APP_LAUNCH_CONTEXT(context));
  g_app_info_launch_default_for_uri_async(gtk_link_button_get_uri(button),
                                         G_APP_LAUNCH_CONTEXT(context), nullptr,
                                         link_opened, g_object_ref(button));
  g_object_unref(context);
  return TRUE;
}
} // namespace

GtkWidget* external_link(const char* uri)
{
  auto* button = gtk_link_button_new(uri);
  g_signal_connect(button, "activate-link", G_CALLBACK(activate_link), nullptr);
  return button;
}
} // namespace studio_remote::linux::ui
