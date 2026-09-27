#include "linux_management_pages.hpp"

#include "linux_ui_helpers.hpp"

namespace studio_remote::linux
{
GtkWidget* make_linux_status_page(LinuxPageContext context)
{
  using namespace ui;
  auto* result = page("Status", "Review Companion health and control production adapters.");
  auto* shutdown = button("Shutdown Companion",
                          [context]
                          {
                            confirm(context.window,
                                    "Shut down Companion",
                                    "Shut down PaneLive Companion and disconnect all mobile "
                                    "clients?",
                                    [context] { context.controller.shutdown(); });
                          });
  gtk_widget_set_name(shutdown, "shutdown-companion");
  gtk_widget_set_halign(shutdown, GTK_ALIGN_END);
  append(result, shutdown);

  append(result, section_title("Backend adapters"));
  auto* adapters = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
  for (const auto& adapter : context.controller.state().adapters)
  {
    auto* adapter_row = flow_row();
    gtk_widget_set_margin_top(adapter_row, 8);
    gtk_widget_set_margin_bottom(adapter_row, 8);
    auto* identity = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(identity, TRUE);
    auto* name = gtk_label_new(adapter.name.c_str());
    gtk_label_set_xalign(GTK_LABEL(name), 0.0F);
    gtk_widget_add_css_class(name, "heading");
    append(identity, name);
    append(identity, description(adapter.detail));
    flow_append(adapter_row, identity);
    flow_append(
        adapter_row,
        badge(adapter.gateway_state, adapter.gateway_state == "running" ? "healthy" : "neutral"));
    flow_append(
        adapter_row,
        badge(adapter.backend_state, adapter.backend_state == "online" ? "healthy" : "warning"));
    flow_append(adapter_row,
                button(
                    "Start",
                    [context, id = adapter.provider_id]
                    {
                      context.controller.start_adapter(id);
                      context.redraw();
                    },
                    adapter.can_start));
    flow_append(adapter_row,
                button(
                    "Stop",
                    [context, id = adapter.provider_id]
                    {
                      context.controller.stop_adapter(id);
                      context.redraw();
                    },
                    adapter.can_stop));
    flow_append(adapter_row,
                button(
                    "Restart",
                    [context, id = adapter.provider_id]
                    {
                      context.controller.restart_adapter(id);
                      context.redraw();
                    },
                    adapter.can_restart));
    append(adapters, adapter_row);
  }
  if (context.controller.state().adapters.empty())
    append(adapters, description("No production adapters are installed."));
  append(result, card(adapters));

  append(result, section_title("Health feeds"));
  auto* feeds = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
  for (const auto& feed : context.controller.state().status_feeds)
  {
    auto* feed_row = flow_row(3);
    gtk_widget_set_margin_top(feed_row, 8);
    gtk_widget_set_margin_bottom(feed_row, 8);
    auto* name = gtk_label_new(feed.name.c_str());
    gtk_label_set_wrap(GTK_LABEL(name), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(name), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0F);
    auto* detail = description(feed.detail);
    gtk_widget_set_hexpand(detail, TRUE);
    flow_append(feed_row, name);
    flow_append(feed_row,
                badge(feed.state,
                      feed.state == "healthy"    ? "healthy"
                      : feed.state == "degraded" ? "warning"
                                                 : "neutral"));
    flow_append(feed_row, detail);
    append(feeds, feed_row);
  }
  if (context.controller.state().status_feeds.empty())
    append(feeds, description("No health feeds are available."));
  append(result, card(feeds));
  return result;
}
} // namespace studio_remote::linux
