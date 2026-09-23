#include "linux_management_pages.hpp"
#include "linux_ui_helpers.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace studio_remote::linux
{
namespace
{
std::string local_datetime(std::uint64_t unix_ms)
{
  const auto seconds = static_cast<std::time_t>(unix_ms / 1000);
  std::tm local{};
  if (!localtime_r(&seconds, &local))
    return "Unavailable";
  std::ostringstream formatted;
  formatted << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
  return formatted.str();
}

std::string client_identity(LinuxPageContext context)
{
  const auto& state = context.controller.teleprompter().state();
  const auto& clients = context.controller.state().clients;
  const auto client = std::ranges::find(clients, state.device_id, &ManagementClientRow::id);
  if (client == clients.end())
    return state.client_name;
  auto identity = client->name + " | Adapter: " + client->backend;
  if (!client->identification_code.empty())
    identity += " | Identification code: " + client->identification_code;
  return identity;
}

GtkWidget* library_actions(LinuxPageContext context, const teleprompter::Summary& script)
{
  auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  ui::append(actions,
             ui::button("Edit",
                        [context, id = script.id]
                        {
                          context.controller.teleprompter().edit_script(id);
                          context.redraw();
                        }));
  ui::append(actions,
             ui::button("Delete",
                        [context, script]
                        {
                          ui::confirm(context.window,
                                      "Delete teleprompter script",
                                      "Delete '" + script.name +
                                          "' from the library and all attached clients?",
                                      [context, id = script.id, revision = script.revision]
                                      {
                                        context.controller.teleprompter().delete_script(id,
                                                                                        revision);
                                        context.controller.refresh();
                                        context.redraw();
                                      });
                        }));
  return actions;
}

GtkWidget* script_row(LinuxPageContext context, const teleprompter::Summary& script)
{
  const auto& state = context.controller.teleprompter().state();
  const bool library = state.view == TeleprompterView::library;
  const bool attached = std::ranges::find(state.scripts, script.id, &teleprompter::Summary::id) !=
                        state.scripts.end();
  auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
  auto* info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_hexpand(info, TRUE);
  if (!library)
  {
    auto* badge =
        ui::badge(attached ? "Attached" : "Not attached", attached ? "healthy" : "neutral");
    gtk_widget_set_halign(badge, GTK_ALIGN_START);
    ui::append(info, badge);
    if (!attached)
      gtk_widget_add_css_class(info, "unattached-script-info");
  }
  auto* name = ui::section_title(script.name);
  gtk_label_set_wrap(GTK_LABEL(name), TRUE);
  ui::append(info, name);
  ui::append(info,
             ui::description("Created " + local_datetime(script.created_at_unix_ms) +
                             "    Modified " + local_datetime(script.modified_at_unix_ms) +
                             " (local time)"));
  ui::append(info,
             ui::description(std::to_string(script.word_count) + " words | Approx. " +
                             std::to_string((script.estimated_duration_ms + 999) / 1000) +
                             " seconds"));
  ui::append(row, info);
  if (library)
    ui::append(row, library_actions(context, script));
  else
  {
    auto* assignment = ui::button(attached ? "Detach" : "Attach",
                                  [context, id = script.id, attached]
                                  {
                                    if (attached)
                                      context.controller.teleprompter().detach_script(id);
                                    else
                                      context.controller.teleprompter().attach_script(id);
                                    context.controller.refresh();
                                    context.redraw();
                                  });
    gtk_widget_set_name(assignment, attached ? "teleprompter-detach" : "teleprompter-attach");
    gtk_widget_set_valign(assignment, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(assignment, attached ? "script-detach" : "script-attach");
    const auto label = std::string(attached ? "Detach " : "Attach ") + script.name;
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(assignment), GTK_ACCESSIBLE_PROPERTY_LABEL, label.c_str(), -1);
    ui::append(row, assignment);
  }
  auto* card = ui::card(row);
  if (!library && attached)
    gtk_widget_add_css_class(card, "attached-script");
  return card;
}

void append_library(GtkWidget* body, LinuxPageContext context)
{
  const auto& state = context.controller.teleprompter().state();
  if (state.view == TeleprompterView::library)
    ui::append(body,
               ui::button("Add teleprompter script",
                          [context]
                          {
                            context.controller.teleprompter().add_script();
                            context.redraw();
                          }));
  else
  {
    ui::append(
        body,
        ui::description(
            "Attach library scripts to this client. Create and edit scripts in Teleprompter."));
    ui::append(body, make_linux_teleprompter_sharing_actions(context));
  }
  if (state.library.empty())
    ui::append(body, ui::description("No scripts yet. Add a script in Teleprompter."));
  for (const auto& script : state.library)
    ui::append(body, script_row(context, script));
}
} // namespace

GtkWidget* make_linux_teleprompter_page(LinuxPageContext context)
{
  const auto& state = context.controller.teleprompter().state();
  const bool library = state.view == TeleprompterView::library;
  const auto title = state.sharing  ? "Set up players"
                     : state.editor ? (state.editor->script_id ? "Edit teleprompter script"
                                                               : "Add teleprompter script")
                                    : "Teleprompter";
  auto* page = ui::page(title,
                        library ? "Manage scripts here. Attach them to devices from Clients."
                                : client_identity(context));
  if (!library)
    ui::append(page,
               ui::button("Back to clients",
                          [context]
                          {
                            const auto close = [context]
                            {
                              context.controller.teleprompter().close();
                              context.redraw();
                            };
                            if (context.controller.teleprompter().has_unsaved_changes())
                              ui::confirm(context.window,
                                          "Discard script changes",
                                          "Discard the unsaved teleprompter script changes?",
                                          close);
                            else
                              close();
                          }));
  auto* error = ui::description(state.error);
  gtk_widget_add_css_class(error, "error");
  ui::append(page, error);
  g_object_set_data(G_OBJECT(page), "teleprompter-error", error);
  if (state.sharing)
    ui::append(page, make_linux_teleprompter_sharing(context));
  else if (state.editor)
    ui::append(page, make_linux_teleprompter_editor(context));
  else
    append_library(page, context);
  refresh_linux_teleprompter_page(page, context);
  return page;
}

void refresh_linux_teleprompter_page(GtkWidget* page, LinuxPageContext context)
{
  auto* error = GTK_WIDGET(g_object_get_data(G_OBJECT(page), "teleprompter-error"));
  const auto& message = context.controller.teleprompter().state().error;
  gtk_label_set_text(GTK_LABEL(error), message.c_str());
  gtk_widget_set_visible(error, !message.empty());
}
} // namespace studio_remote::linux
