#include "linux_ui_helpers.hpp"

#include <exception>
#include <utility>

namespace studio_remote::linux::ui
{
namespace
{
struct ClickAction
{
  std::function<void()> action;
  std::function<void()> alternate;
};

struct ToggleAction
{
  std::function<void(bool)> action;
};

void on_clicked(GtkButton*, gpointer data)
{
  // Redraw may destroy the source button and its closure while this action runs.
  try
  {
    const auto action = static_cast<ClickAction*>(data)->action;
    action();
  }
  catch (const std::exception& error)
  {
    g_warning("Management action failed: %s", error.what());
  }
  catch (...)
  {
    g_warning("Management action failed");
  }
}

gboolean on_state_set(GtkSwitch* widget, gboolean state, gpointer data)
{
  try
  {
    const auto action = static_cast<ToggleAction*>(data)->action;
    action(state != FALSE);
  }
  catch (const std::exception& error)
  {
    g_warning("Management toggle failed: %s", error.what());
  }
  catch (...)
  {
    g_warning("Management toggle failed");
  }
  // A confirmation or failed save may have restored the observed position during the
  // callback. Do not let GTK's default handler reapply the original requested value.
  gtk_switch_set_state(widget, gtk_switch_get_active(widget));
  return TRUE;
}

void delete_click_action(gpointer data, GClosure*)
{
  delete static_cast<ClickAction*>(data);
}

void delete_toggle_action(gpointer data, GClosure*)
{
  delete static_cast<ToggleAction*>(data);
}

void on_confirmation_response(GtkDialog* dialog, int response, gpointer data)
{
  const auto action = *static_cast<ClickAction*>(data);
  if (response == GTK_RESPONSE_ACCEPT)
    action.action();
  else if (action.alternate)
    action.alternate();
  gtk_window_destroy(GTK_WINDOW(dialog));
}

} // namespace

void append(GtkWidget* box, GtkWidget* child)
{
  gtk_box_append(GTK_BOX(box), child);
}

GtkWidget* page(std::string_view title, std::string_view detail)
{
  auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_set_margin_top(box, 24);
  gtk_widget_set_margin_bottom(box, 24);
  gtk_widget_set_margin_start(box, 24);
  gtk_widget_set_margin_end(box, 24);
  auto* header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  auto* heading = gtk_label_new(std::string(title).c_str());
  gtk_widget_add_css_class(heading, "title-1");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
  append(header, heading);
  append(header, description(detail));
  append(box, header);
  return box;
}

GtkWidget* card(GtkWidget* contents)
{
  auto* frame = gtk_frame_new(nullptr);
  gtk_widget_add_css_class(frame, "card");
  gtk_widget_set_margin_top(contents, 18);
  gtk_widget_set_margin_bottom(contents, 18);
  gtk_widget_set_margin_start(contents, 18);
  gtk_widget_set_margin_end(contents, 18);
  gtk_frame_set_child(GTK_FRAME(frame), contents);
  return frame;
}

GtkWidget* section_title(std::string_view value)
{
  auto* label = gtk_label_new(std::string(value).c_str());
  gtk_widget_add_css_class(label, "title-3");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
  return label;
}

GtkWidget* description(std::string_view value)
{
  auto* label = gtk_label_new(std::string(value).c_str());
  gtk_widget_add_css_class(label, "dim-label");
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
  return label;
}

GtkWidget* form_row(std::string_view title, GtkWidget* control, std::string_view detail)
{
  auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  auto* labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand(labels, TRUE);
  auto* heading = gtk_label_new(std::string(title).c_str());
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
  append(labels, heading);
  if (!detail.empty())
    append(labels, description(detail));
  append(box, labels);
  gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
  append(box, control);
  return box;
}

GtkWidget* entry(std::string_view value, std::string_view placeholder, bool secret)
{
  auto* result = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(result), std::string(value).c_str());
  gtk_entry_set_placeholder_text(GTK_ENTRY(result), std::string(placeholder).c_str());
  gtk_entry_set_visibility(GTK_ENTRY(result), !secret);
  gtk_widget_set_hexpand(result, TRUE);
  gtk_widget_set_size_request(result, 300, -1);
  return result;
}

GtkWidget* spin(double value, double minimum, double maximum)
{
  auto* result = gtk_spin_button_new_with_range(minimum, maximum, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(result), value);
  gtk_widget_set_size_request(result, 130, -1);
  return result;
}

GtkWidget* button(std::string_view label, std::function<void()> action, bool enabled)
{
  auto* result = gtk_button_new_with_label(std::string(label).c_str());
  gtk_widget_set_sensitive(result, enabled);
  g_signal_connect_data(result,
                        "clicked",
                        G_CALLBACK(on_clicked),
                        new ClickAction{std::move(action), {}},
                        delete_click_action,
                        static_cast<GConnectFlags>(0));
  return result;
}

GtkWidget* toggle(bool active, std::function<void(bool)> action)
{
  auto* result = gtk_switch_new();
  gtk_switch_set_active(GTK_SWITCH(result), active);
  g_signal_connect_data(result,
                        "state-set",
                        G_CALLBACK(on_state_set),
                        new ToggleAction{std::move(action)},
                        delete_toggle_action,
                        static_cast<GConnectFlags>(0));
  return result;
}

GtkWidget* badge(std::string_view value, std::string_view tone)
{
  auto* label = gtk_label_new(std::string(value).c_str());
  gtk_widget_add_css_class(label, "pill");
  gtk_widget_add_css_class(label, std::string(tone).c_str());
  return label;
}

GtkWidget* named(GtkWidget* widget, std::string_view name)
{
  gtk_widget_set_name(widget, std::string(name).c_str());
  return widget;
}

GtkWidget* find_widget(GtkWidget* parent, std::string_view name)
{
  if (name == gtk_widget_get_name(parent))
    return parent;
  for (auto* child = gtk_widget_get_first_child(parent); child;
       child = gtk_widget_get_next_sibling(child))
    if (auto* found = find_widget(child, name))
      return found;
  return nullptr;
}

void refresh_badge(GtkWidget* widget, std::string_view value, std::string_view tone)
{
  gtk_label_set_text(GTK_LABEL(widget), std::string(value).c_str());
  for (const auto* previous : {"neutral", "healthy", "warning", "error"})
    gtk_widget_remove_css_class(widget, previous);
  gtk_widget_add_css_class(widget, std::string(tone).c_str());
}

void refresh_toggle(GtkWidget* widget, bool active)
{
  // Observation must not dispatch a save or reopen a confirmation dialog.
  const auto signal = g_signal_lookup("state-set", GTK_TYPE_SWITCH);
  g_signal_handlers_block_matched(widget, G_SIGNAL_MATCH_ID, signal, 0, nullptr, nullptr, nullptr);
  gtk_switch_set_active(GTK_SWITCH(widget), active);
  g_signal_handlers_unblock_matched(
      widget, G_SIGNAL_MATCH_ID, signal, 0, nullptr, nullptr, nullptr);
}

std::string text(GtkWidget* control)
{
  return gtk_editable_get_text(GTK_EDITABLE(control));
}

std::uint16_t port(GtkWidget* control)
{
  return static_cast<std::uint16_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(control)));
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void confirm(GtkWindow* parent,
             std::string_view title,
             std::string_view message,
             std::function<void()> confirmed,
             std::function<void()> cancelled)
{
  auto* dialog = gtk_message_dialog_new(parent,
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_WARNING,
                                        GTK_BUTTONS_NONE,
                                        "%s",
                                        std::string(message).c_str());
  gtk_window_set_title(GTK_WINDOW(dialog), std::string(title).c_str());
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Continue", GTK_RESPONSE_ACCEPT);
  g_signal_connect_data(dialog,
                        "response",
                        G_CALLBACK(on_confirmation_response),
                        new ClickAction{std::move(confirmed), std::move(cancelled)},
                        delete_click_action,
                        static_cast<GConnectFlags>(0));
  gtk_window_present(GTK_WINDOW(dialog));
}

G_GNUC_END_IGNORE_DEPRECATIONS
} // namespace studio_remote::linux::ui
