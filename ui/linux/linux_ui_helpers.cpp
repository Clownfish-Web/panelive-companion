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

GtkWidget* dispatch_icon(std::string_view name, int size)
{
  const auto icon_name = "dispatch-" + std::string(name) + "-symbolic";
  auto* image = gtk_image_new_from_icon_name(icon_name.c_str());
  gtk_image_set_pixel_size(GTK_IMAGE(image), size);
  return image;
}

FocusSnapshot capture_focus(GtkWindow* window, GtkWidget* content)
{
  auto* focused = gtk_window_get_focus(window);
  if (!focused || !gtk_widget_is_ancestor(focused, content))
    return {};
  // Editable controls often focus an internal GtkText child, not the named public widget.
  while (focused != content &&
         std::string_view(gtk_widget_get_name(focused)) == G_OBJECT_TYPE_NAME(focused))
    focused = gtk_widget_get_parent(focused);
  if (focused == content)
    return {};
  FocusSnapshot snapshot{gtk_widget_get_name(focused), {}};
  if (GTK_IS_EDITABLE(focused))
  {
    int begin = 0;
    int end = 0;
    if (!gtk_editable_get_selection_bounds(GTK_EDITABLE(focused), &begin, &end))
      begin = end = gtk_editable_get_position(GTK_EDITABLE(focused));
    snapshot.selection = std::pair{begin, end};
  }
  return snapshot;
}

void restore_focus(GtkWindow* window, GtkWidget* content, const FocusSnapshot& snapshot)
{
  if (snapshot.widget_name.empty())
    return;
  auto* target = find_widget(content, snapshot.widget_name);
  if (!target || !gtk_widget_is_sensitive(target) || !gtk_widget_is_visible(target))
    return;
  auto* current = gtk_window_get_focus(window);
  // Retained form controls must keep their selection and active input-method composition.
  if (current && (current == target || gtk_widget_is_ancestor(current, target)))
    return;
  gtk_widget_grab_focus(target);
  if (snapshot.selection && GTK_IS_EDITABLE(target))
    gtk_editable_select_region(
        GTK_EDITABLE(target), snapshot.selection->first, snapshot.selection->second);
}

GtkWidget* page(std::string_view title, std::string_view detail)
{
  auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  auto* header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  auto* header_layout = flow_row(2);
  gtk_widget_add_css_class(box, "dispatch-page");
  gtk_widget_add_css_class(header_layout, "dispatch-page-heading");
  auto* overline = description("YOUR PRODUCTION, CONNECTED");
  gtk_widget_add_css_class(overline, "dispatch-overline");
  append(header, overline);
  auto* heading = gtk_label_new(std::string(title).c_str());
  gtk_widget_add_css_class(heading, "title-1");
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
  gtk_label_set_wrap(GTK_LABEL(heading), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(heading), PANGO_WRAP_WORD_CHAR);
  append(header, heading);
  append(header, description(detail));
  flow_append(header_layout, header);
  append(box, header_layout);
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
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  return label;
}

GtkWidget* description(std::string_view value)
{
  auto* label = gtk_label_new(std::string(value).c_str());
  gtk_widget_add_css_class(label, "dim-label");
  gtk_label_set_wrap(GTK_LABEL(label), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
  return label;
}

GtkWidget* form_row(std::string_view title, GtkWidget* control, std::string_view detail)
{
  auto* box = flow_row(2);
  auto* labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand(labels, TRUE);
  auto* heading = gtk_label_new(std::string(title).c_str());
  gtk_label_set_xalign(GTK_LABEL(heading), 0.0F);
  gtk_label_set_wrap(GTK_LABEL(heading), TRUE);
  gtk_label_set_wrap_mode(GTK_LABEL(heading), PANGO_WRAP_WORD_CHAR);
  append(labels, heading);
  gtk_accessible_update_relation(GTK_ACCESSIBLE(control),
                                 GTK_ACCESSIBLE_RELATION_LABELLED_BY,
                                 heading,
                                 nullptr,
                                 -1);
  if (!detail.empty())
  {
    auto* help = description(detail);
    append(labels, help);
    gtk_accessible_update_relation(GTK_ACCESSIBLE(control),
                                   GTK_ACCESSIBLE_RELATION_DESCRIBED_BY,
                                   help,
                                   nullptr,
                                   -1);
  }
  flow_append(box, labels);
  gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
  flow_append(box, control);
  return box;
}

GtkWidget* flow_row(unsigned int maximum_columns)
{
  auto* row = gtk_flow_box_new();
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(row), GTK_SELECTION_NONE);
  gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(row), 1);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(row), maximum_columns);
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(row), 12);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(row), 8);
  return row;
}

void flow_append(GtkWidget* row, GtkWidget* child)
{
  gtk_flow_box_insert(GTK_FLOW_BOX(row), child, -1);
}

GtkWidget* entry(std::string_view value, std::string_view placeholder, bool secret)
{
  auto* result = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(result), std::string(value).c_str());
  gtk_entry_set_placeholder_text(GTK_ENTRY(result), std::string(placeholder).c_str());
  gtk_entry_set_visibility(GTK_ENTRY(result), !secret);
  gtk_widget_set_hexpand(result, TRUE);
  gtk_widget_set_size_request(result, 180, -1);
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
  // Composite buttons supply their own child and accessible name. A temporary GtkLabel
  // would leave a stale LABELLED_BY relation when that child is replaced.
  auto* result = label.empty() ? gtk_button_new()
                               : gtk_button_new_with_label(std::string(label).c_str());
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
  gtk_widget_set_halign(result, GTK_ALIGN_END);
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
