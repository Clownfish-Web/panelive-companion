#include "linux_management_window.hpp"

#include <exception>

namespace studio_remote::linux
{
void LinuxManagementWindow::IdentificationWindowDeleter::operator()(
    GtkWindow* window) const noexcept
{
  gtk_window_destroy(window);
}

void LinuxManagementWindow::cancel_client_identification()
{
  if (identification_dialog_)
    g_signal_handlers_disconnect_by_data(identification_dialog_.get(), this);
  identification_dialog_.reset();
  identification_audible_ = nullptr;
  identification_vibrate_ = nullptr;
  controller_.cancel_client_identification();
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void LinuxManagementWindow::show_client_identification()
{
  const auto& request = controller_.state().identification;
  if (!request || identification_dialog_)
    return;
  identification_dialog_.reset(GTK_WINDOW(gtk_dialog_new()));
  auto* dialog = identification_dialog_.get();
  gtk_window_set_transient_for(dialog, window_);
  gtk_window_set_modal(dialog, TRUE);
  gtk_window_set_title(dialog, request->title.c_str());
  gtk_window_set_default_size(dialog, 340, -1);
  auto* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  gtk_box_set_spacing(GTK_BOX(content), 12);
  gtk_widget_set_margin_top(content, 20);
  gtk_widget_set_margin_bottom(content, 20);
  gtk_widget_set_margin_start(content, 20);
  gtk_widget_set_margin_end(content, 20);
  auto* visual = gtk_check_button_new_with_label("Visual (required)");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(visual), TRUE);
  gtk_widget_set_sensitive(visual, FALSE);
  identification_audible_ = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Audible"));
  identification_vibrate_ = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Vibrate"));
  gtk_check_button_set_active(identification_audible_, ClientIdentificationOptions{}.audible);
  gtk_check_button_set_active(identification_vibrate_, ClientIdentificationOptions{}.vibrate);
  gtk_box_append(GTK_BOX(content), visual);
  gtk_box_append(GTK_BOX(content), GTK_WIDGET(identification_audible_));
  gtk_box_append(GTK_BOX(content), GTK_WIDGET(identification_vibrate_));
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Identify", GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
  g_signal_connect(
      dialog,
      "response",
      G_CALLBACK(
          +[](GtkDialog*, int response, gpointer data)
          { static_cast<LinuxManagementWindow*>(data)->finish_client_identification(response); }),
      this);
  gtk_window_present(dialog);
}
G_GNUC_END_IGNORE_DEPRECATIONS

void LinuxManagementWindow::finish_client_identification(int response) noexcept
{
  try
  {
    const ClientIdentificationOptions options{
        gtk_check_button_get_active(identification_audible_) != FALSE,
        gtk_check_button_get_active(identification_vibrate_) != FALSE};
    g_signal_handlers_disconnect_by_data(identification_dialog_.get(), this);
    identification_dialog_.reset();
    identification_audible_ = nullptr;
    identification_vibrate_ = nullptr;
    if (response == GTK_RESPONSE_ACCEPT)
      controller_.confirm_client_identification(options);
    else
      controller_.cancel_client_identification();
    redraw();
  }
  catch (const std::exception& error)
  {
    controller_.cancel_client_identification();
    g_warning("Client identification dialog failed: %s", error.what());
  }
  catch (...)
  {
    controller_.cancel_client_identification();
    g_warning("Client identification dialog failed");
  }
}
} // namespace studio_remote::linux
