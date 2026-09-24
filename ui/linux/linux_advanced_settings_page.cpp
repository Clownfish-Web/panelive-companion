#include "linux_management_pages.hpp"

#include "linux_ui_helpers.hpp"

#include <sstream>

namespace studio_remote::linux
{
namespace
{
std::string endurance_summary(const ManagementEnduranceStatus& status)
{
  std::ostringstream output;
  if (!status.file.empty())
    output << "File: " << status.file << '\n';
  output << "Records: " << status.records_written << "  Bytes: " << status.bytes_written
         << "  Retained bytes: " << status.retained_bytes
         << "  Queued bytes: " << status.queued_bytes << "  Queued: " << status.queued_events
         << "  Dropped: " << status.dropped_events;
  return output.str();
}

struct ModeContext
{
  LinuxPageContext page;
};

void mode_changed(GObject* object, GParamSpec*, gpointer data)
{
  auto& context = *static_cast<ModeContext*>(data);
  const auto selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
  const auto mode =
      selected == 1 ? ManagementInterfaceMode::advanced : ManagementInterfaceMode::basic;
  if (mode == context.page.controller.state().interface_mode)
    return;
  context.page.controller.set_interface_mode(mode);
  context.page.redraw();
}

void delete_mode_context(gpointer data, GClosure*)
{
  delete static_cast<ModeContext*>(data);
}

void automatic_headroom_changed(GObject* object, GParamSpec*, gpointer data)
{
  gtk_widget_set_sensitive(GTK_WIDGET(data), gtk_switch_get_active(GTK_SWITCH(object)));
}

} // namespace

GtkWidget* make_linux_advanced_settings_page(LinuxPageContext context)
{
  using namespace ui;
  auto* result =
      page("Advanced Settings", "Choose the interface and configure Companion-wide behavior.");
  const auto& state = context.controller.state();
  const bool advanced = state.interface_mode == ManagementInterfaceMode::advanced;

  auto* interface = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  append(interface, section_title("Interface"));
  const char* modes[] = {"Basic", "Advanced", nullptr};
  auto* mode = gtk_drop_down_new_from_strings(modes);
  named(mode, "interface-mode");
  gtk_drop_down_set_selected(GTK_DROP_DOWN(mode), advanced ? 1 : 0);
  append(interface,
         form_row("Choose which day-to-day and advanced controls are visible.",
                  mode,
                  "Advanced mode reveals network, diagnostics, audio, and installation "
                  "settings."));
  g_signal_connect_data(mode,
                        "notify::selected",
                        G_CALLBACK(mode_changed),
                        new ModeContext{context},
                        delete_mode_context,
                        static_cast<GConnectFlags>(0));
  append(result, card(interface));

  auto* access = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  append(access, section_title("Access control"));
  auto* single_operator =
      toggle(state.basic_settings.single_operator_mode,
             [context](bool enabled)
             {
               const bool original = context.controller.state().basic_settings.single_operator_mode;
               if (enabled && !original)
               {
                 context.redraw();
                 confirm(
                     context.window,
                     "Enable one operator per production backend",
                     "For each backend with multiple operators, the most recently active operator "
                     "will remain and every other operator will immediately become a viewer.",
                     [context]
                     {
                       context.controller.set_single_operator_mode(true);
                       context.redraw();
                     },
                     context.redraw);
                 return;
               }
               if (enabled != original)
                 context.controller.set_single_operator_mode(enabled);
               context.redraw();
             });
  named(single_operator, "single-operator");
  append(access,
         form_row("Limit each production backend to one operator client.",
                  single_operator,
                  "Other paired clients remain viewers."));
  append(result, card(access));

  auto* background = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  append(background, section_title("Background operation"));
  auto* startup = toggle(state.basic_settings.sign_in_startup,
                         [context](bool enabled)
                         {
                           if (enabled == context.controller.state().basic_settings.sign_in_startup)
                             return;
                           context.controller.set_sign_in_startup(enabled);
                           context.redraw();
                         });
  named(startup, "sign-in-startup");
  append(background,
         form_row("Start PaneLive Companion automatically when this user signs in.", startup));
  append(result, card(background));

  {
    auto* debug = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    append(debug, section_title("Debug diagnostics"));
    auto* recording = toggle(state.endurance_status.enabled,
                             [context](bool enabled)
                             {
                               if (enabled == context.controller.state().endurance_status.enabled)
                                 return;
                               context.controller.set_endurance_logging(enabled);
                               context.redraw();
                             });
    gtk_widget_set_name(recording, "endurance-toggle");
    append(debug,
           form_row(
               "Record debug diagnostics",
               recording,
               "Default off. Keeps at most 24 hours and 64 MiB. Stops after 24 hours or restart."));
    append(debug, named(description({}), "endurance-summary"));
    append(debug, named(badge({}, "error"), "endurance-error"));
    append(debug,
           button("Clear debug logs",
                  [context]
                  {
                    context.controller.clear_debug_logs();
                    context.redraw();
                  }));
    append(result, named(card(debug), "debug-card"));
  }

  auto* audio = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  append(audio, section_title("Audio"));
  append(audio,
         named(description(
                   advanced ? "Configure mixing headroom and the Preview-only source audition bus."
                            : "Control whether monitored Preview sources stay active for private "
                              "auditioning."),
               "audio-description"));
  auto* automatic = toggle(state.advanced_settings.automatic_headroom, [](bool) {});
  auto* headroom = spin(state.advanced_settings.headroom_per_additional_source_db, 1, 4);
  named(automatic, "automatic-headroom");
  named(headroom, "headroom");
  gtk_widget_set_sensitive(headroom, state.advanced_settings.automatic_headroom);
  g_signal_connect(automatic, "notify::active", G_CALLBACK(automatic_headroom_changed), headroom);
  auto* audition = toggle(state.advanced_settings.audio_source_audition_bus, [](bool) {});
  named(audition, "audition-bus");
  {
    append(
        audio,
        named(form_row("Automatic summing headroom",
                       automatic,
                       "Reduce each additional monitored source to protect the combined mix from "
                       "clipping."),
              "automatic-headroom-row"));
    append(audio,
           named(form_row("Attenuation per additional source", headroom, "dB"), "headroom-row"));
  }
  append(audio,
         form_row("Audio source audition bus",
                  audition,
                  "Hear audio from monitored OBS sources in Preview before they go live. They "
                  "stay out of Program and the broadcast mix."));
  auto* apply_audio = button(
      advanced ? "Apply audio settings" : "Apply audio setting",
      [context, automatic, headroom, audition]
      {
        ManagementAudioMixSettingsRequest request;
        request.automatic_headroom = gtk_switch_get_active(GTK_SWITCH(automatic));
        request.headroom_per_additional_source_db =
            static_cast<std::uint32_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(headroom)));
        request.audio_source_audition_bus = gtk_switch_get_active(GTK_SWITCH(audition));
        const auto apply = [context, request]
        {
          context.controller.apply_audio_mix(request);
          context.redraw();
        };
        if (request.audio_source_audition_bus &&
            !context.controller.state().advanced_settings.audio_source_audition_bus)
        {
          confirm(context.window,
                  "Audio source audition bus",
                  "Monitored Preview-only sources will remain active and may be audible through "
                  "the OBS monitoring device. They are not inserted into Program.",
                  apply,
                  context.redraw);
        }
        else
          apply();
      });
  named(apply_audio, "apply-audio");
  gtk_widget_add_css_class(apply_audio, "suggested-action");
  gtk_widget_set_halign(apply_audio, GTK_ALIGN_END);
  append(audio, apply_audio);
  append(result, card(audio));
  {
    auto* locations = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    append(locations, section_title("Production software locations"));
    auto* obs_directory = gtk_label_new(state.advanced_settings.obs_directory.c_str());
    named(obs_directory, "obs-directory");
    gtk_label_set_xalign(GTK_LABEL(obs_directory), 0.0F);
    gtk_label_set_ellipsize(GTK_LABEL(obs_directory), PANGO_ELLIPSIZE_END);
    gtk_label_set_selectable(GTK_LABEL(obs_directory), TRUE);
    append(locations, form_row("OBS directory", obs_directory));
    append(result, named(card(locations), "locations-card"));
  }
  refresh_linux_advanced_settings_page(result, context);
  return result;
}

void refresh_linux_advanced_settings_page(GtkWidget* page, LinuxPageContext context)
{
  using namespace ui;
  const auto& state = context.controller.state();
  const bool advanced = state.interface_mode == ManagementInterfaceMode::advanced;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(find_widget(page, "interface-mode")), advanced ? 1 : 0);
  refresh_toggle(find_widget(page, "single-operator"), state.basic_settings.single_operator_mode);
  refresh_toggle(find_widget(page, "sign-in-startup"), state.basic_settings.sign_in_startup);
  refresh_toggle(find_widget(page, "endurance-toggle"), state.endurance_status.enabled);
  gtk_label_set_text(GTK_LABEL(find_widget(page, "endurance-summary")),
                     endurance_summary(state.endurance_status).c_str());
  auto* error = find_widget(page, "endurance-error");
  gtk_label_set_text(GTK_LABEL(error), state.endurance_status.last_error.c_str());
  gtk_widget_set_visible(error, !state.endurance_status.last_error.empty());
  for (const auto* name :
       {"debug-card", "locations-card", "automatic-headroom-row", "headroom-row"})
    gtk_widget_set_visible(find_widget(page, name), advanced);
  gtk_label_set_text(
      GTK_LABEL(find_widget(page, "audio-description")),
      advanced ? "Configure mixing headroom and the Preview-only source audition bus."
               : "Control whether monitored Preview sources stay active for private auditioning.");
  gtk_button_set_label(GTK_BUTTON(find_widget(page, "apply-audio")),
                       advanced ? "Apply audio settings" : "Apply audio setting");
  gtk_label_set_text(GTK_LABEL(find_widget(page, "obs-directory")),
                     state.advanced_settings.obs_directory.c_str());
}
} // namespace studio_remote::linux
