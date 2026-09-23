#include "linux_management_pages.hpp"

#include "linux_ui_helpers.hpp"

#include <array>
#include <string>

namespace studio_remote::linux
{
namespace
{
struct Attribution
{
  std::string_view component;
  std::string_view license;
};

constexpr std::array attributions{
    Attribution{"CivetWeb 1.16", "MIT"},
    Attribution{"FFmpeg 9.0.1", "LGPL-2.1-or-later"},
    Attribution{"libdatachannel 0.24.5 and libjuice 1.7.2", "MPL-2.0"},
    Attribution{"libqrencode 4.1.1", "LGPL-2.1-or-later"},
    Attribution{"libsrtp 2.8.0", "BSD-3-Clause"},
    Attribution{"libvpx 1.16.0", "BSD-3-Clause"},
    Attribution{"nlohmann/json 3.12.0", "MIT"},
    Attribution{"OpenSSL 3.6.3", "Apache-2.0"},
    Attribution{"Opus 1.5.2", "BSD-3-Clause"},
    Attribution{"plog 1.1.11", "MIT"},
    Attribution{"SQLite 3.53.4", "Public Domain"},
    Attribution{"usrsctp 0.9.5.0", "BSD-3-Clause"},
    Attribution{"zlib 1.3.2", "Zlib"}};
} // namespace

GtkWidget* make_linux_about_page(std::string_view version)
{
  using namespace ui;
  auto* result = page("About", "Build information, licensing, and acknowledgements.");
  auto* product = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  auto* heading = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  auto* application_icon =
      gtk_image_new_from_resource("/com/clownfishweb/PaneLiveCompanion/icon-128.png");
  gtk_image_set_pixel_size(GTK_IMAGE(application_icon), 72);
  gtk_widget_set_valign(application_icon, GTK_ALIGN_START);
  append(heading, application_icon);
  append(heading, section_title("PaneLive Companion"));
  append(heading, badge("Beta", "warning"));
  append(product, heading);
  append(product, description("Version " + std::string(version)));
  append(product,
         description("Native sidecar gateway for secure remote production monitoring and "
                     "control."));
  append(product, gtk_label_new("Licensed under the Apache License 2.0."));
  append(product, description("Copyright 2026 Clownfish Web Artur Cichosz and contributors."));
  append(result, card(product));

  append(result, section_title("Open-source acknowledgements"));
  append(result,
         description("PaneLive Companion is built with the following open-source software."));
  auto* acknowledgements = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  for (const auto& attribution : attributions)
  {
    auto* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    auto* component = gtk_label_new(std::string(attribution.component).c_str());
    gtk_label_set_wrap(GTK_LABEL(component), TRUE);
    gtk_label_set_xalign(GTK_LABEL(component), 0.0F);
    gtk_widget_set_hexpand(component, TRUE);
    auto* license = description(attribution.license);
    gtk_widget_set_size_request(license, 180, -1);
    append(row, component);
    append(row, license);
    append(acknowledgements, row);
  }
  append(result, card(acknowledgements));
  append(
      result,
      description("Complete notices and license texts are provided in THIRD_PARTY_NOTICES.md "
                  "and the installed share/panelive-companion/licenses directory."));
  return result;
}
} // namespace studio_remote::linux
