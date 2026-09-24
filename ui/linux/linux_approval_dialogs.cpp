#include "linux_approval_dialogs.hpp"

#include <stdexcept>

namespace studio_remote::linux
{
void LinuxApprovalDialogs::WindowDeleter::operator()(GtkWindow* window) const noexcept
{
  gtk_window_destroy(window);
  g_object_unref(window);
}

LinuxApprovalDialogs::~LinuxApprovalDialogs()
{
  close();
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void LinuxApprovalDialogs::show(GtkWindow* parent,
                                std::string device_id,
                                std::string request_id,
                                std::string_view title,
                                std::string_view message,
                                ManagementApprovalDecision decision)
{
  const Key key{std::move(device_id), std::move(request_id)};
  if (pending_.contains(key))
    throw std::logic_error("Native approval is already pending");
  gtk_window_present(parent);
  auto* dialog = gtk_message_dialog_new(parent,
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_QUESTION,
                                        GTK_BUTTONS_NONE,
                                        "%s",
                                        std::string(message).c_str());
  g_object_ref_sink(dialog);
  Pending pending{std::unique_ptr<GtkWindow, WindowDeleter>(GTK_WINDOW(dialog)),
                  std::move(decision)};
  gtk_window_set_title(GTK_WINDOW(dialog), std::string(title).c_str());
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Deny", GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Approve", GTK_RESPONSE_ACCEPT);
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
  struct Response
  {
    LinuxApprovalDialogs* owner;
    Key key;
  };
  g_signal_connect_data(
      dialog,
      "response",
      G_CALLBACK(+[](GtkDialog*, int response, gpointer data) noexcept
                 {
                   // Completion destroys the dialog and its signal closure.
                   const auto context = *static_cast<Response*>(data);
                   context.owner->finish(context.key, response == GTK_RESPONSE_ACCEPT);
                 }),
      new Response{this, key},
      +[](gpointer data, GClosure*) { delete static_cast<Response*>(data); },
      static_cast<GConnectFlags>(0));
  pending_.emplace(key, std::move(pending));
  gtk_window_present(GTK_WINDOW(dialog));
}
G_GNUC_END_IGNORE_DEPRECATIONS

void LinuxApprovalDialogs::finish(const Key& key, bool approved) noexcept
{
  const auto found = pending_.find(key);
  if (found == pending_.end())
    return;
  auto decision = std::move(found->second.decision);
  pending_.erase(found);
  try
  {
    decision(approved);
  }
  catch (...)
  {
    g_warning("Could not deliver the native approval result");
  }
}

void LinuxApprovalDialogs::cancel(std::string_view device_id, std::string_view request_id)
{
  finish({std::string(device_id), std::string(request_id)}, false);
}

void LinuxApprovalDialogs::close()
{
  while (!pending_.empty())
  {
    const auto key = pending_.begin()->first;
    finish(key, false);
  }
}
} // namespace studio_remote::linux
