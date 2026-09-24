#include "linux_management_pages.hpp"

#include "linux_ui_helpers.hpp"

#include <sstream>
#include <utility>

namespace studio_remote::linux
{
namespace
{
struct GainContext
{
  LinuxPageContext page;
  GtkLabel* value;
};

void gain_changed(GtkRange* range, gpointer data)
{
  auto& context = *static_cast<GainContext*>(data);
  const auto gain = static_cast<float>(gtk_range_get_value(range));
  const auto text = std::to_string(static_cast<int>(gain)) + " dB";
  gtk_label_set_text(context.value, text.c_str());
  context.page.controller.set_audio_gain(gain);
}

void delete_gain_context(gpointer data, GClosure*)
{
  delete static_cast<GainContext*>(data);
}

struct SaveContext
{
  std::string report;
};

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void save_response(GtkNativeDialog* dialog, int response, gpointer data)
{
  if (response == GTK_RESPONSE_ACCEPT)
  {
    auto* file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
    if (auto* path = g_file_get_path(file))
    {
      GError* error = nullptr;
      static_cast<void>(
          g_file_set_contents(path, static_cast<SaveContext*>(data)->report.c_str(), -1, &error));
      if (error)
        g_error_free(error);
      g_free(path);
    }
    g_object_unref(file);
  }
  g_object_unref(dialog);
}
G_GNUC_END_IGNORE_DEPRECATIONS

void delete_save_context(gpointer data, GClosure*)
{
  delete static_cast<SaveContext*>(data);
}

std::string audio_summary(const ManagementAudioDiagnosticStatus& status)
{
  if (!status.error.empty())
    return status.error;
  if (!status.playing)
    return "Audio diagnostics are idle.";
  return "Playing on " + status.device_name + " - received " +
         std::to_string(status.received_frames) + " frames, played " +
         std::to_string(status.played_frames) + ".";
}
} // namespace

GtkWidget* make_linux_diagnostics_page(LinuxPageContext context)
{
  using namespace ui;
  auto* result = page("Diagnostics", "Run local audio checks.");
  const auto& state = context.controller.state();

  auto* audio = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  append(audio, section_title("Local audio diagnostics"));
  std::vector<std::pair<std::string, std::string>> choices;
  std::string selected;
  for (const auto& device : state.audio_outputs)
  {
    choices.emplace_back(device.id, device.name);
    if (device.is_default || selected.empty())
      selected = device.id;
  }
  auto* output = named(gtk_drop_down_new(nullptr, nullptr), "audio-output");
  refresh_choices(output, std::move(choices), selected);
  auto* output_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_hexpand(output, TRUE);
  append(output_row, output);
  append(output_row,
         button("Refresh devices",
                [context]
                {
                  context.controller.refresh();
                  context.redraw();
                }));
  append(audio, output_row);

  auto* gain_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  auto* gain_label = gtk_label_new("Gain");
  auto* gain = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -24, 12, 1);
  named(gain, "audio-gain");
  gtk_range_set_value(GTK_RANGE(gain), state.audio_status.gain_db);
  gtk_widget_set_hexpand(gain, TRUE);
  const auto gain_text = std::to_string(static_cast<int>(state.audio_status.gain_db)) + " dB";
  auto* gain_value = GTK_LABEL(gtk_label_new(gain_text.c_str()));
  append(gain_row, gain_label);
  append(gain_row, gain);
  append(gain_row, GTK_WIDGET(gain_value));
  g_signal_connect_data(gain,
                        "value-changed",
                        G_CALLBACK(gain_changed),
                        new GainContext{context, gain_value},
                        delete_gain_context,
                        static_cast<GConnectFlags>(0));
  append(audio, gain_row);

  const auto start = [context, output](ManagementAudioDiagnosticTap tap)
  {
    const auto selected = selected_choice(output);
    if (selected.empty())
      return;
    context.controller.start_audio(selected, tap);
    context.redraw();
  };
  auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  append(
      actions,
      named(button("Play pre-Opus IPC", [start] { start(ManagementAudioDiagnosticTap::pre_opus); }),
            "play-pre-opus"));
  append(actions,
         button("Play post-Opus", [start] { start(ManagementAudioDiagnosticTap::post_opus); }));
  append(actions,
         named(button(
                   "Stop",
                   [context]
                   {
                     context.controller.stop_audio();
                     context.redraw();
                   },
                   state.audio_status.playing),
               "audio-stop"));
  append(
      actions,
      named(button(state.audio_status.debug_log_active ? "Complete debug log" : "Begin debug log",
                   [context]
                   {
                     if (context.controller.state().audio_status.debug_log_active)
                       context.controller.complete_audio_debug_log();
                     else
                       context.controller.begin_audio_debug_log();
                     context.redraw();
                   }),
            "audio-debug"));
  append(actions,
         named(button(
                   "Save report",
                   [context]
                   {
                     auto* chooser = gtk_file_chooser_native_new("Save audio diagnostics",
                                                                 context.window,
                                                                 GTK_FILE_CHOOSER_ACTION_SAVE,
                                                                 "Save",
                                                                 "Cancel");
                     gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser),
                                                       "panelive-audio-diagnostics.jsonl");
                     g_signal_connect_data(chooser,
                                           "response",
                                           G_CALLBACK(save_response),
                                           new SaveContext{context.controller.audio_debug_report()},
                                           delete_save_context,
                                           static_cast<GConnectFlags>(0));
                     gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
                   },
                   state.audio_status.debug_log_available),
               "audio-save"));
  G_GNUC_END_IGNORE_DEPRECATIONS
  append(audio, actions);

  auto* maximum = gtk_progress_bar_new();
  named(maximum, "audio-maximum");
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(maximum),
                                state.audio_status.maximum_peak_last_second);
  append(audio, form_row("Maximum peak", maximum));
  auto* average = gtk_progress_bar_new();
  named(average, "audio-average");
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(average),
                                state.audio_status.average_peak_last_second);
  append(audio, form_row("Average peak", average));
  append(audio, named(description(audio_summary(state.audio_status)), "audio-status"));
  append(result, card(audio));
  return result;
}

void refresh_linux_diagnostics_page(GtkWidget* page, LinuxPageContext context)
{
  using namespace ui;
  const auto& state = context.controller.state();
  std::vector<std::pair<std::string, std::string>> choices;
  for (const auto& device : state.audio_outputs)
    choices.emplace_back(device.id, device.name);
  refresh_choices(find_widget(page, "audio-output"), std::move(choices));
  gtk_widget_set_sensitive(find_widget(page, "audio-stop"), state.audio_status.playing);
  gtk_button_set_label(GTK_BUTTON(find_widget(page, "audio-debug")),
                       state.audio_status.debug_log_active ? "Complete debug log"
                                                           : "Begin debug log");
  gtk_widget_set_sensitive(find_widget(page, "audio-save"), state.audio_status.debug_log_available);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(find_widget(page, "audio-maximum")),
                                state.audio_status.maximum_peak_last_second);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(find_widget(page, "audio-average")),
                                state.audio_status.average_peak_last_second);
  gtk_label_set_text(GTK_LABEL(find_widget(page, "audio-status")),
                     audio_summary(state.audio_status).c_str());
}
} // namespace studio_remote::linux
