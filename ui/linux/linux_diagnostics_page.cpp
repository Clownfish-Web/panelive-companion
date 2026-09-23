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
  auto* outputs = gtk_string_list_new(nullptr);
  guint selected = GTK_INVALID_LIST_POSITION;
  for (std::size_t index = 0; index < state.audio_outputs.size(); ++index)
  {
    gtk_string_list_append(outputs, state.audio_outputs[index].name.c_str());
    if (state.audio_outputs[index].is_default)
      selected = static_cast<guint>(index);
  }
  auto* output = gtk_drop_down_new(G_LIST_MODEL(outputs), nullptr);
  if (selected == GTK_INVALID_LIST_POSITION && !state.audio_outputs.empty())
    selected = 0;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(output), selected);
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
                        G_CONNECT_DEFAULT);
  append(audio, gain_row);

  const auto start =
      [context, output, devices = state.audio_outputs](ManagementAudioDiagnosticTap tap)
  {
    const auto index = gtk_drop_down_get_selected(GTK_DROP_DOWN(output));
    if (index >= devices.size())
      return;
    context.controller.start_audio(devices[index].id, tap);
    context.redraw();
  };
  auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  append(actions,
         button("Play pre-Opus IPC", [start] { start(ManagementAudioDiagnosticTap::pre_opus); }));
  append(actions,
         button("Play post-Opus", [start] { start(ManagementAudioDiagnosticTap::post_opus); }));
  append(actions,
         button(
             "Stop",
             [context]
             {
               context.controller.stop_audio();
               context.redraw();
             },
             state.audio_status.playing));
  append(actions,
         button(state.audio_status.debug_log_active ? "Complete debug log" : "Begin debug log",
                [context, active = state.audio_status.debug_log_active]
                {
                  if (active)
                    context.controller.complete_audio_debug_log();
                  else
                    context.controller.begin_audio_debug_log();
                  context.redraw();
                }));
  append(actions,
         button(
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
                                     G_CONNECT_DEFAULT);
               gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
             },
             state.audio_status.debug_log_available));
  G_GNUC_END_IGNORE_DEPRECATIONS
  append(audio, actions);

  auto* maximum = gtk_progress_bar_new();
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(maximum),
                                state.audio_status.maximum_peak_last_second);
  append(audio, form_row("Maximum peak", maximum));
  auto* average = gtk_progress_bar_new();
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(average),
                                state.audio_status.average_peak_last_second);
  append(audio, form_row("Average peak", average));
  append(audio, description(audio_summary(state.audio_status)));
  append(result, card(audio));
  return result;
}
} // namespace studio_remote::linux
