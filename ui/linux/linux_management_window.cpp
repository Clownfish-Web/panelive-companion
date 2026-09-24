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
  bool footer = false;
};

constexpr std::array navigation_items{
    NavigationItem{ManagementPage::clients, "Clients", "network-workgroup-symbolic"},
    NavigationItem{ManagementPage::adapters, "Adapters", "network-transmit-receive-symbolic"},
    NavigationItem{ManagementPage::teleprompter, "Teleprompter", "text-x-generic-symbolic"},
    NavigationItem{ManagementPage::companion_settings, "Companion Settings", "computer-symbolic"},
    NavigationItem{ManagementPage::status, "Status", "emblem-ok-symbolic"},
    NavigationItem{ManagementPage::diagnostics, "Diagnostics", "utilities-system-monitor-symbolic"},
    NavigationItem{ManagementPage::advanced_settings,
                   "Advanced Settings",
                   "preferences-system-symbolic",
                   true},
    NavigationItem{ManagementPage::about, "About", "help-about-symbolic", true}};

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
  auto* content_overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(content_overlay, TRUE);
  gtk_overlay_set_child(GTK_OVERLAY(content_overlay), content_);
  pairing_preview_host_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(pairing_preview_host_, "pairing-preview");
  gtk_widget_add_css_class(pairing_preview_host_, "pairing-preview-backdrop");
  gtk_widget_set_visible(pairing_preview_host_, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(content_overlay), pairing_preview_host_);
  gtk_box_append(GTK_BOX(body), content_overlay);
  gtk_box_append(GTK_BOX(shell), body);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), shell);
  create_content();
  redraw();
}

LinuxManagementWindow::~LinuxManagementWindow()
{
  if (window_)
    close();
}

void LinuxManagementWindow::activate()
{
  if (!window_)
    return;
  controller_.refresh();
  redraw();
  gtk_window_present(window_);
}

void LinuxManagementWindow::hide()
{
  close_pairing_preview();
  cancel_client_identification();
  gtk_widget_set_visible(GTK_WIDGET(window_), FALSE);
}

void LinuxManagementWindow::close()
{
  if (!window_)
    return;
  close_pairing_preview();
  role_dialogs_.close();
  global_dialogs_.close();
  cancel_client_identification();
  pages_.reset();
  gtk_window_destroy(window_);
  window_ = nullptr;
}

void LinuxManagementWindow::request_role_change(ManagementRoleChangeRequest request,
                                                ManagementApprovalDecision decision)
{
  role_dialogs_.show(window_,
                     request.device_id,
                     request.request_id,
                     "Client role request",
                     "Allow '" + request.device_name + "' to become " + request.requested_role +
                         " for " + request.backend_name + "?",
                     std::move(decision));
  activate();
}

void LinuxManagementWindow::request_global_setting_change(
    ManagementGlobalSettingChangeRequest request, ManagementApprovalDecision decision)
{
  global_dialogs_.show(window_,
                       request.device_id,
                       request.request_id,
                       "Global setting request",
                       "'" + request.device_name + "' requested " + request.setting_name + ".\n\n" +
                           request.warning,
                       std::move(decision));
  activate();
}

void LinuxManagementWindow::cancel_role_change(std::string_view device, std::string_view request)
{
  role_dialogs_.cancel(device, request);
}

void LinuxManagementWindow::cancel_global_setting_change(std::string_view device,
                                                         std::string_view request)
{
  global_dialogs_.cancel(device, request);
}

void LinuxManagementWindow::redraw()
{
  if (!window_)
    return;
  current_page_ = visible_management_page(controller_.state().interface_mode, current_page_);
  update_pairing_preview();
  rebuild_navigation();
  const LinuxPageContext context{
      controller_, window_, [this] { redraw(); }, [this] { open_pairing_preview(); }};
  gtk_label_set_text(GTK_LABEL(error_bar_), controller_.state().last_error.c_str());
  gtk_widget_set_visible(error_bar_, !controller_.state().last_error.empty());
  const auto& network = controller_.state().network_settings;
  gtk_widget_set_visible(recovery_card_,
                         !network.listening &&
                             controller_.state().interface_mode == ManagementInterfaceMode::basic);
  gtk_label_set_text(GTK_LABEL(recovery_message_),
                     network.warning.empty() ? "The Companion listener needs attention."
                                             : network.warning.c_str());
  pages_->show(current_page_, context);
  show_client_identification();
}

void LinuxManagementWindow::clear_pairing()
{
  close_pairing_preview();
  controller_.clear_pairing();
  if (current_page_ == ManagementPage::clients)
    redraw();
}

void LinuxManagementWindow::create_content()
{
  error_bar_ = ui::badge(controller_.state().last_error, "error");
  gtk_widget_set_name(error_bar_, "error-bar");
  gtk_widget_set_margin_top(error_bar_, 12);
  gtk_widget_set_margin_start(error_bar_, 24);
  gtk_widget_set_margin_end(error_bar_, 24);
  gtk_widget_set_visible(error_bar_, !controller_.state().last_error.empty());
  gtk_box_append(GTK_BOX(content_), error_bar_);
  auto* recovery = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top(recovery, 12);
  gtk_widget_set_margin_start(recovery, 24);
  gtk_widget_set_margin_end(recovery, 24);
  auto* message = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  gtk_widget_set_hexpand(message, TRUE);
  gtk_box_append(GTK_BOX(message), ui::section_title("Companion listener needs attention"));
  recovery_message_ = ui::description({});
  gtk_box_append(GTK_BOX(message), recovery_message_);
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
  recovery_card_ = ui::card(recovery);
  gtk_widget_add_css_class(recovery_card_, "warning");
  gtk_box_append(GTK_BOX(content_), recovery_card_);
  auto* scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(content_), scroll);
  pages_ = std::make_unique<LinuxManagementPagesHost>(GTK_SCROLLED_WINDOW(scroll), version_);
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
  close_pairing_preview();
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
    if (item.page == ManagementPage::clients)
      gtk_widget_set_name(button, "navigation-clients");
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(button, "flat");
    if (item.page == current_page_)
      gtk_widget_add_css_class(button, "selected");
    auto* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    auto* icon = gtk_image_new_from_icon_name(std::string(item.icon).c_str());
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 18);
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

} // namespace studio_remote::linux
