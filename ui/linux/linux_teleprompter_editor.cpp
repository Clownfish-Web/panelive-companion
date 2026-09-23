#include "linux_management_pages.hpp"
#include "linux_ui_helpers.hpp"

#include <cmath>
#include <exception>
#include <memory>

namespace studio_remote::linux
{
namespace
{
// Each GTK signal owns its callback until the source widget is destroyed. Copy before invoking:
// a command can redraw the page and release the signal's closure during the callback.
using EditAction = std::function<void()>;
void changed(GObject*, gpointer data) noexcept
{
  try
  {
    const auto action = *static_cast<EditAction*>(data);
    action();
  }
  catch (const std::exception& error)
  {
    g_warning("Teleprompter editor callback failed: %s", error.what());
  }
  catch (...)
  {
    g_warning("Teleprompter editor callback failed");
  }
}
void bind_change(gpointer source, const char* signal, EditAction action)
{
  auto owned = std::make_unique<EditAction>(std::move(action));
  g_signal_connect_data(
      source,
      signal,
      G_CALLBACK(changed),
      owned.release(),
      [](gpointer data, GClosure*) { delete static_cast<EditAction*>(data); },
      G_CONNECT_DEFAULT);
}

std::string bounded_text(const char* text, std::size_t maximum_units)
{
  const char* end = text;
  std::size_t units = 0;
  while (*end)
  {
    const auto next_units = g_utf8_get_char(end) > 0xffff ? 2U : 1U;
    if (units + next_units > maximum_units)
      break;
    units += next_units;
    end = g_utf8_next_char(end);
  }
  return {text, static_cast<std::size_t>(end - text)};
}

GtkWidget* name_field(LinuxPageContext context, const teleprompter::Draft& draft)
{
  auto* name = ui::entry(draft.name);
  gtk_entry_set_max_length(GTK_ENTRY(name), 160);
  gtk_widget_set_name(name, "teleprompter-name");
  bind_change(name,
              "changed",
              [context, name]
              {
                const auto original = ui::text(name);
                const auto bounded = bounded_text(original.c_str(), 160);
                if (bounded != original)
                  gtk_editable_set_text(GTK_EDITABLE(name), bounded.c_str());
                context.controller.teleprompter().set_name(bounded);
              });
  return name;
}

GtkWidget* content_field(LinuxPageContext context, const teleprompter::Draft& draft)
{
  auto* content = gtk_text_view_new();
  gtk_widget_set_name(content, "teleprompter-content");
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(content), GTK_WRAP_WORD_CHAR);
  auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(content));
  gtk_text_buffer_set_text(buffer, draft.content.c_str(), static_cast<int>(draft.content.size()));
  bind_change(buffer,
              "changed",
              [context, buffer]
              {
                GtkTextIter start;
                GtkTextIter end;
                gtk_text_buffer_get_bounds(buffer, &start, &end);
                const std::unique_ptr<gchar, decltype(&g_free)> text(
                    gtk_text_buffer_get_text(buffer, &start, &end, FALSE), &g_free);
                const auto bounded = bounded_text(text.get(), teleprompter::maximum_content_bytes);
                if (bounded != text.get())
                  gtk_text_buffer_set_text(
                      buffer, bounded.c_str(), static_cast<int>(bounded.size()));
                context.controller.teleprompter().set_content(bounded);
              });
  auto* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 300);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll), 420);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);
  return scroll;
}

GtkWidget* speed_field(LinuxPageContext context, const teleprompter::Draft& draft)
{
  auto* speed = ui::spin(draft.words_per_minute, 40, 400);
  gtk_widget_set_name(speed, "teleprompter-speed");
  bind_change(speed,
              "value-changed",
              [context, speed]
              {
                const auto value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(speed));
                context.controller.teleprompter().set_reading_speed(
                    std::isfinite(value) && value >= 40 && value <= 400 &&
                            std::floor(value) == value
                        ? static_cast<std::uint32_t>(value)
                        : 0);
              });
  return speed;
}
} // namespace

GtkWidget* make_linux_teleprompter_editor(LinuxPageContext context)
{
  const auto& draft = context.controller.teleprompter().state().editor->draft;
  auto* form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  ui::append(form, ui::form_row("Script name", name_field(context, draft)));
  ui::append(form, ui::section_title("Script content"));
  ui::append(form, content_field(context, draft));
  ui::append(
      form,
      ui::description(
          "Put [PAUSE:5] on its own line to hold at the center for five seconds with a countdown "
          "(1-60 seconds). Use [cue:Introduction] for a visible chapter marker on the player's "
          "timeline. Bracketed instructions are not spoken."));
  ui::append(form, ui::form_row("Reading speed (words per minute)", speed_field(context, draft)));
  auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  ui::append(actions,
             ui::button("Save",
                        [context]
                        {
                          context.controller.teleprompter().save_script();
                          context.redraw();
                        }));
  ui::append(actions,
             ui::button("Cancel",
                        [context]
                        {
                          const auto discard = [context]
                          {
                            context.controller.teleprompter().cancel_edit();
                            context.redraw();
                          };
                          if (context.controller.teleprompter().has_unsaved_changes())
                            ui::confirm(context.window,
                                        "Discard script changes",
                                        "Discard the unsaved teleprompter script changes?",
                                        discard);
                          else
                            discard();
                        }));
  ui::append(form, actions);
  return form;
}
} // namespace studio_remote::linux
