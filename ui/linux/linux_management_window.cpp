#include "linux_management_window.hpp"

#include "linux_management_pages.hpp"
#include "linux_ui_helpers.hpp"

#include <array>

namespace studio_remote::linux
{
namespace
{
struct NavigationItem
{
  ManagementPage page;
  std::string_view label;
  std::string_view icon;
  std::string_view secondary_icon;
  bool footer = false;
};

constexpr std::array navigation_items{
    NavigationItem{
        ManagementPage::clients, "Clients", "network-cellular-symbolic", "input-tablet-symbolic"},
    NavigationItem{ManagementPage::adapters, "Adapters", "network-transmit-receive-symbolic", ""},
    NavigationItem{ManagementPage::teleprompter, "Teleprompter", "text-x-generic-symbolic", ""},
    NavigationItem{
        ManagementPage::companion_settings, "Companion Settings", "computer-symbolic", ""},
    NavigationItem{ManagementPage::status, "Status", "emblem-ok-symbolic", ""},
    NavigationItem{
        ManagementPage::diagnostics, "Diagnostics", "utilities-system-monitor-symbolic", ""},
    NavigationItem{ManagementPage::advanced_settings,
                   "Advanced Settings",
                   "preferences-system-symbolic",
                   "",
                   true},
    NavigationItem{ManagementPage::about, "About", "help-about-symbolic", "", true}};

bool same_sharing_selection(const std::optional<teleprompter::SessionAssignment>& previous,
                            const std::optional<teleprompter::SessionAssignment>& current)
{
  if (!previous || !current)
    return previous.has_value() == current.has_value();
  return previous->script_ids == current->script_ids && previous->members == current->members &&
         previous->owner_role == current->owner_role;
}

void remove_children(GtkWidget* container)
{
  while (auto* child = gtk_widget_get_first_child(container))
    gtk_box_remove(GTK_BOX(container), child);
}

gboolean close_requested(GtkWindow*, gpointer data)
{
  static_cast<LinuxManagementWindow*>(data)->hide();
  return TRUE;
}
} // namespace

LinuxManagementWindow::LinuxManagementWindow(AdwApplication* application,
                                             std::string companion_name,
                                             std::string version,
                                             ManagementUiController& controller)
    : companion_name_(std::move(companion_name)), version_(std::move(version)),
      controller_(controller), current_page_(controller.state().initial_page)
{
  window_ = GTK_WINDOW(adw_application_window_new(GTK_APPLICATION(application)));
  gtk_window_set_title(window_, companion_name_.c_str());
  gtk_window_set_icon_name(window_, "com.clownfishweb.PaneLiveCompanion");
  gtk_window_set_default_size(window_, 1040, 720);
  g_signal_connect(window_, "close-request", G_CALLBACK(close_requested), this);

  auto* shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  auto* header = adw_header_bar_new();
  auto* application_icon =
      gtk_image_new_from_resource("/com/clownfishweb/PaneLiveCompanion/icon-32.png");
  gtk_image_set_pixel_size(GTK_IMAGE(application_icon), 28);
  gtk_widget_set_tooltip_text(application_icon, "PaneLive Companion");
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), application_icon);
  auto* title = adw_window_title_new(companion_name_.c_str(), "Management");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);
  gtk_box_append(GTK_BOX(shell), header);

  auto* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_vexpand(body, TRUE);
  navigation_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_size_request(navigation_, 208, -1);
  gtk_widget_set_hexpand(navigation_, FALSE);
  gtk_widget_add_css_class(navigation_, "navigation-sidebar");
  content_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(content_, TRUE);
  gtk_box_append(GTK_BOX(body), navigation_);
  auto* separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_box_append(GTK_BOX(body), separator);
  gtk_box_append(GTK_BOX(body), content_);
  gtk_box_append(GTK_BOX(shell), body);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), shell);
  redraw();
}

void LinuxManagementWindow::activate()
{
  gtk_window_present(window_);
}

void LinuxManagementWindow::hide()
{
  cancel_client_identification();
  gtk_widget_set_visible(GTK_WIDGET(window_), FALSE);
}

void LinuxManagementWindow::close()
{
  cancel_client_identification();
  gtk_window_destroy(window_);
  window_ = nullptr;
}

void LinuxManagementWindow::request_role_change(ManagementRoleChangeRequest request,
                                                ManagementApprovalDecision decision)
{
  ui::approve(window_,
              "Client role request",
              "Allow '" + request.device_name + "' to become " + request.requested_role + " for " +
                  request.backend_name + "?",
              std::move(decision));
  activate();
}

void LinuxManagementWindow::request_global_setting_change(
    ManagementGlobalSettingChangeRequest request, ManagementApprovalDecision decision)
{
  ui::approve(window_,
              "Global setting request",
              "'" + request.device_name + "' requested " + request.setting_name + ".\n\n" +
                  request.warning,
              std::move(decision));
  activate();
}

void LinuxManagementWindow::redraw()
{
  current_page_ = visible_management_page(controller_.state().interface_mode, current_page_);
  rebuild_navigation();
  const LinuxPageContext context{controller_, window_, [this] { redraw(); }};
  const auto& teleprompter = controller_.teleprompter().state();
  const bool retain =
      (current_page_ == ManagementPage::clients || current_page_ == ManagementPage::teleprompter) &&
      (teleprompter.editor || teleprompter.sharing);
  if (retain && teleprompter_form_ &&
      teleprompter_form_generation_ == teleprompter.form_generation &&
      same_sharing_selection(teleprompter_sharing_selection_, teleprompter.sharing))
  {
    gtk_label_set_text(GTK_LABEL(error_bar_), controller_.state().last_error.c_str());
    gtk_widget_set_visible(error_bar_, !controller_.state().last_error.empty());
    refresh_linux_teleprompter_page(teleprompter_form_, context);
    return;
  }
  teleprompter_form_ = nullptr;
  remove_children(content_);
  error_bar_ = ui::badge(controller_.state().last_error, "error");
  gtk_widget_set_name(error_bar_, "error-bar");
  gtk_widget_set_margin_top(error_bar_, 12);
  gtk_widget_set_margin_start(error_bar_, 24);
  gtk_widget_set_margin_end(error_bar_, 24);
  gtk_widget_set_visible(error_bar_, !controller_.state().last_error.empty());
  gtk_box_append(GTK_BOX(content_), error_bar_);
  const auto& network = controller_.state().network_settings;
  if (!network.listening && controller_.state().interface_mode == ManagementInterfaceMode::basic)
  {
    auto* recovery = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(recovery, 12);
    gtk_widget_set_margin_start(recovery, 24);
    gtk_widget_set_margin_end(recovery, 24);
    auto* message = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(message, TRUE);
    gtk_box_append(GTK_BOX(message), ui::section_title("Companion listener needs attention"));
    gtk_box_append(GTK_BOX(message),
                   ui::description(network.warning.empty()
                                       ? "The Companion listener needs attention."
                                       : network.warning));
    gtk_box_append(GTK_BOX(recovery), message);
    auto* open_settings =
        ui::button("Open Companion Settings",
                   [this]
                   {
                     controller_.set_interface_mode(ManagementInterfaceMode::advanced);
                     select_page(ManagementPage::companion_settings);
                   });
    gtk_widget_set_valign(open_settings, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(recovery), open_settings);
    auto* recovery_card = ui::card(recovery);
    gtk_widget_add_css_class(recovery_card, "warning");
    gtk_box_append(GTK_BOX(content_), recovery_card);
  }
  auto* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  auto* page = make_current_page();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), page);
  if (retain)
  {
    teleprompter_form_ = page;
    teleprompter_form_generation_ = teleprompter.form_generation;
    teleprompter_sharing_selection_ = teleprompter.sharing;
  }
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(content_), scroll);
  show_client_identification();
}

GtkWindow* LinuxManagementWindow::window() const noexcept
{
  return window_;
}

ManagementPage LinuxManagementWindow::current_page() const noexcept
{
  return current_page_;
}

void LinuxManagementWindow::select_page(ManagementPage page)
{
  if (page != current_page_ && controller_.teleprompter().has_unsaved_changes())
  {
    ui::confirm(window_,
                "Discard script changes",
                "Discard the unsaved teleprompter script changes?",
                [this, page]
                {
                  controller_.teleprompter().close();
                  select_page(page);
                });
    return;
  }
  if (page != current_page_)
  {
    controller_.teleprompter().close();
    if (page == ManagementPage::teleprompter)
      controller_.teleprompter().open_library();
  }
  current_page_ = visible_management_page(controller_.state().interface_mode, page);
  redraw();
}

void LinuxManagementWindow::rebuild_navigation()
{
  remove_children(navigation_);
  auto* brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class(brand, "navigation-brand");
  auto* logo = gtk_image_new_from_resource("/com/clownfishweb/PaneLiveCompanion/icon-32.png");
  gtk_image_set_pixel_size(GTK_IMAGE(logo), 32);
  gtk_widget_set_tooltip_text(logo, "PaneLive Companion");
  gtk_box_append(GTK_BOX(brand), logo);
  auto* title = gtk_label_new("PaneLive");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0F);
  gtk_widget_add_css_class(title, "navigation-title");
  gtk_box_append(GTK_BOX(brand), title);
  gtk_box_append(GTK_BOX(navigation_), brand);
  auto* primary = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  auto* footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_vexpand(primary, TRUE);
  for (const auto& item : navigation_items)
  {
    if (!management_page_visible(controller_.state().interface_mode, item.page))
      continue;
    auto* button = ui::button(item.label, [this, page = item.page] { select_page(page); });
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(button, "flat");
    if (item.page == current_page_)
      gtk_widget_add_css_class(button, "selected");
    auto* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    auto* icon = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    auto* primary_icon = gtk_image_new_from_icon_name(std::string(item.icon).c_str());
    gtk_image_set_pixel_size(GTK_IMAGE(primary_icon), item.secondary_icon.empty() ? 18 : 15);
    gtk_box_append(GTK_BOX(icon), primary_icon);
    if (!item.secondary_icon.empty())
    {
      auto* secondary_icon = gtk_image_new_from_icon_name(std::string(item.secondary_icon).c_str());
      gtk_image_set_pixel_size(GTK_IMAGE(secondary_icon), 12);
      gtk_box_append(GTK_BOX(icon), secondary_icon);
    }
    gtk_widget_set_size_request(icon, 24, 20);
    auto* label = gtk_label_new(std::string(item.label).c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(content), icon);
    gtk_box_append(GTK_BOX(content), label);
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_box_append(GTK_BOX(item.footer ? footer : primary), button);
  }
  gtk_box_append(GTK_BOX(navigation_), primary);
  auto* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(navigation_), separator);
  gtk_box_append(GTK_BOX(navigation_), footer);
}

GtkWidget* LinuxManagementWindow::make_current_page()
{
  const LinuxPageContext context{controller_, window_, [this] { redraw(); }};
  switch (current_page_)
  {
  case ManagementPage::clients:
    return make_linux_clients_page(context);
  case ManagementPage::teleprompter:
    if (controller_.teleprompter().state().view != TeleprompterView::library)
      controller_.teleprompter().open_library();
    return make_linux_teleprompter_page(context);
  case ManagementPage::adapters:
    return make_linux_adapters_page(context);
  case ManagementPage::companion_settings:
    return make_linux_companion_settings_page(context);
  case ManagementPage::status:
    return make_linux_status_page(context);
  case ManagementPage::diagnostics:
    return make_linux_diagnostics_page(context);
  case ManagementPage::advanced_settings:
    return make_linux_advanced_settings_page(context);
  case ManagementPage::about:
    return make_linux_about_page(version_);
  }
  return make_linux_clients_page(context);
}
} // namespace studio_remote::linux
