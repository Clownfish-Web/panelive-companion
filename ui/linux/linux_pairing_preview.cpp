#include "linux_management_window.hpp"

#include "linux_pairing_qr.hpp"
#include "linux_ui_helpers.hpp"

#include <chrono>

namespace studio_remote::linux
{
namespace
{
std::uint64_t unix_seconds()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

GtkWidget* make_preview_panel(const PairingQrImage& image, const std::function<void()>& close)
{
  auto* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_add_css_class(panel, "pairing-preview-panel");
  gtk_widget_set_halign(panel, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(panel, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(panel, TRUE);
  auto* heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  auto* title = ui::section_title("Pair a new client");
  gtk_widget_set_hexpand(title, TRUE);
  ui::append(heading, title);
  auto* dismiss = ui::named(ui::button("", close), "pairing-preview-close");
  gtk_button_set_child(GTK_BUTTON(dismiss), gtk_image_new_from_icon_name("window-close-symbolic"));
  gtk_widget_set_size_request(dismiss, 36, 36);
  gtk_widget_set_tooltip_text(dismiss, "Dismiss enlarged QR (Esc)");
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(dismiss), GTK_ACCESSIBLE_PROPERTY_LABEL, "Close enlarged pairing QR", -1);
  ui::append(heading, dismiss);
  ui::append(panel, heading);
  ui::append(panel,
             ui::named(make_linux_pairing_image(image, PairingPreview::enlarged_size),
                       "pairing-preview-image"));
  ui::append(panel, ui::named(gtk_label_new(""), "pairing-preview-countdown"));
  return panel;
}
} // namespace

void LinuxManagementWindow::open_pairing_preview()
{
  close_pairing_preview();
  if (current_page_ != ManagementPage::clients || !controller_.state().pairing)
    return;
  try
  {
    pairing_preview_source_ = controller_.state().pairing;
    pairing_preview_image_ = make_pairing_qr_image(pairing_preview_source_->payload);
    pairing_preview_.open(pairing_preview_image_.pixels,
                          pairing_preview_source_->expires_unix_seconds,
                          std::chrono::steady_clock::now(),
                          unix_seconds());
    auto* panel = make_preview_panel(pairing_preview_image_, [this] { close_pairing_preview(); });
    pairing_preview_countdown_ = ui::find_widget(panel, "pairing-preview-countdown");
    auto* escape = gtk_event_controller_key_new();
    g_signal_connect(
        escape,
        "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey*, guint key, guint, GdkModifierType, gpointer data)
                       -> gboolean
                   {
                     if (key != GDK_KEY_Escape)
                       return FALSE;
                     static_cast<LinuxManagementWindow*>(data)->close_pairing_preview();
                     return TRUE;
                   }),
        this);
    gtk_widget_add_controller(panel, escape);
    gtk_box_append(GTK_BOX(pairing_preview_host_), panel);
    gtk_widget_set_visible(pairing_preview_host_, TRUE);
    gtk_widget_set_sensitive(content_, FALSE);
    update_pairing_preview();
    if (!pairing_preview_source_)
      return;
    pairing_preview_timer_ = g_timeout_add(
        100,
        +[](gpointer data) -> gboolean
        {
          auto* self = static_cast<LinuxManagementWindow*>(data);
          try
          {
            self->update_pairing_preview();
          }
          catch (...)
          {
            g_warning("Could not update enlarged pairing QR");
            self->close_pairing_preview();
          }
          return self->pairing_preview_timer_ ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
        },
        this);
    gtk_widget_grab_focus(ui::find_widget(panel, "pairing-preview-close"));
  }
  catch (...)
  {
    close_pairing_preview();
    controller_.report_error("Could not display enlarged pairing QR");
    g_warning("Could not display enlarged pairing QR");
    redraw();
  }
}

void LinuxManagementWindow::update_pairing_preview()
{
  if (!pairing_preview_source_)
    return;
  const auto& current = controller_.state().pairing;
  if (current_page_ != ManagementPage::clients || !current ||
      current->payload != pairing_preview_source_->payload ||
      current->expires_unix_seconds != pairing_preview_source_->expires_unix_seconds)
  {
    close_pairing_preview();
    return;
  }
  const auto remaining = pairing_preview_.seconds_remaining(pairing_preview_image_.pixels,
                                                            current->expires_unix_seconds,
                                                            std::chrono::steady_clock::now(),
                                                            unix_seconds());
  if (remaining == 0)
  {
    close_pairing_preview();
    return;
  }
  const auto text = "Closing in " + std::to_string(remaining) + " seconds";
  if (text != gtk_label_get_text(GTK_LABEL(pairing_preview_countdown_)))
    gtk_label_set_text(GTK_LABEL(pairing_preview_countdown_), text.c_str());
}

void LinuxManagementWindow::close_pairing_preview()
{
  if (pairing_preview_timer_ != 0)
    g_source_remove(pairing_preview_timer_);
  pairing_preview_timer_ = 0;
  pairing_preview_.close();
  pairing_preview_source_.reset();
  pairing_preview_image_ = {};
  gtk_widget_set_visible(pairing_preview_host_, FALSE);
  while (auto* child = gtk_widget_get_first_child(pairing_preview_host_))
    gtk_box_remove(GTK_BOX(pairing_preview_host_), child);
  pairing_preview_countdown_ = nullptr;
  gtk_widget_set_sensitive(content_, TRUE);
}
} // namespace studio_remote::linux
