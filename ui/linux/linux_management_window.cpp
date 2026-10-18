#include "linux_management_window.hpp"

#include "linux_management_pages.hpp"
#include "linux_ui_helpers.hpp"
#include "client_stores.hpp"

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
    NavigationItem{ManagementPage::clients, "Clients", "clients"},
    NavigationItem{ManagementPage::adapters, "Adapters", "adapters"},
    NavigationItem{ManagementPage::teleprompter, "Teleprompter", "teleprompter"},
    NavigationItem{ManagementPage::companion_settings, "Companion Settings", "settings"},
    NavigationItem{ManagementPage::status, "Status", "status"},
    NavigationItem{ManagementPage::diagnostics, "Diagnostics", "diagnostics"},
    NavigationItem{ManagementPage::advanced_settings, "Advanced Settings", "advanced", true},
    NavigationItem{ManagementPage::about, "About", "about", true}};

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
  gtk_icon_theme_add_resource_path(
      gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(window_))),
      "/com/clownfishweb/PaneLiveCompanion/icons");
  gtk_widget_add_css_class(GTK_WIDGET(window_), "dispatch");
  gtk_window_set_title(window_, companion_name_.c_str());
  gtk_window_set_icon_name(window_, "com.clownfishweb.PaneLiveCompanion");
  gtk_window_set_default_size(window_, 1040, 720);
  g_signal_connect(window_, "close-request", G_CALLBACK(close_requested), this);
  g_signal_connect(
      window_,
      "realize",
      G_CALLBACK(+[](GtkWidget*, gpointer data) noexcept
                 { static_cast<LinuxManagementWindow*>(data)->observe_window_size(); }),
      this);
  g_signal_connect(
      window_,
      "unrealize",
      G_CALLBACK(+[](GtkWidget*, gpointer data) noexcept
                 { static_cast<LinuxManagementWindow*>(data)->stop_observing_window_size(); }),
      this);

  auto* shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  const auto omarchy_current = std::string(g_get_home_dir()) + "/.local/state/omarchy/current";
  if (!g_file_test(omarchy_current.c_str(), G_FILE_TEST_IS_DIR))
  {
    auto* header = adw_header_bar_new();
    auto* application_icon =
        gtk_image_new_from_resource("/com/clownfishweb/PaneLiveCompanion/icon-32.png");
    gtk_image_set_pixel_size(GTK_IMAGE(application_icon), 28);
    gtk_widget_set_tooltip_text(application_icon, "PaneLive Companion");
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), application_icon);
    auto* title = adw_window_title_new(companion_name_.c_str(), "Management");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);
    gtk_box_append(GTK_BOX(shell), header);
  }

  auto* body = gtk_overlay_new();
  gtk_widget_set_vexpand(body, TRUE);
  auto* content_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_overlay_set_child(GTK_OVERLAY(body), content_row);
  navigation_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_hexpand(navigation_, FALSE);
  gtk_widget_add_css_class(navigation_, "navigation-sidebar");
  navigation_scroll_ = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(navigation_scroll_), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(navigation_scroll_), navigation_);
  gtk_widget_set_halign(navigation_scroll_, GTK_ALIGN_START);
  gtk_overlay_add_overlay(GTK_OVERLAY(body), navigation_scroll_);
  navigation_space_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(content_row), navigation_space_);
  content_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(content_, TRUE);
  auto* separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
  gtk_box_append(GTK_BOX(content_row), separator);
  auto* content_overlay = gtk_overlay_new();
  gtk_widget_set_hexpand(content_overlay, TRUE);
  gtk_overlay_set_child(GTK_OVERLAY(content_overlay), content_);
  pairing_preview_host_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(pairing_preview_host_, "pairing-preview");
  gtk_widget_add_css_class(pairing_preview_host_, "pairing-preview-backdrop");
  gtk_widget_set_visible(pairing_preview_host_, FALSE);
  gtk_overlay_add_overlay(GTK_OVERLAY(content_overlay), pairing_preview_host_);
  gtk_box_append(GTK_BOX(content_row), content_overlay);
  gtk_box_append(GTK_BOX(shell), body);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_), shell);
  create_navigation();
  auto* keys = gtk_event_controller_key_new();
  g_signal_connect(
      keys,
      "key-pressed",
      G_CALLBACK(
          +[](GtkEventControllerKey*, guint key, guint, GdkModifierType, gpointer data) noexcept
              -> gboolean
          {
            auto& owner = *static_cast<LinuxManagementWindow*>(data);
            if (key != GDK_KEY_Escape || !owner.narrow_window_ || !owner.navigation_expanded_)
              return FALSE;
            owner.navigation_expanded_ = false;
            owner.apply_navigation_layout();
            gtk_widget_grab_focus(owner.navigation_toggle_);
            return TRUE;
          }),
      this);
  gtk_widget_add_controller(GTK_WIDGET(window_), keys);
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
  cancel_layout_redraw();
}

void LinuxManagementWindow::close()
{
  if (!window_)
    return;
  close_pairing_preview();
  role_dialogs_.close();
  global_dialogs_.close();
  cancel_client_identification();
  stop_observing_window_size();
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
  const auto focus = ui::capture_focus(window_, content_);
  current_page_ = visible_management_page(controller_.state().interface_mode, current_page_);
  update_pairing_preview();
  rebuild_navigation();
  const LinuxPageContext context{controller_,
                                 window_,
                                 [this] { redraw(); },
                                 [this] { open_pairing_preview(); },
                                 selected_client_id_,
                                 [this](std::string id)
                                 {
                                   selected_client_id_ = std::move(id);
                                   redraw();
                                 },
                                 gtk_widget_get_width(content_),
                                 client_query_,
                                 [this](std::string query) { client_query_ = std::move(query); }};
  gtk_label_set_text(GTK_LABEL(error_bar_), controller_.state().last_error.c_str());
  gtk_widget_set_visible(error_bar_, !controller_.state().last_error.empty());
  const auto& network = controller_.state().network_settings;
  gtk_label_set_text(GTK_LABEL(footer_status_),
                     network.listening ? "● Companion listening" : "○ Companion not listening");
  gtk_widget_set_visible(recovery_card_,
                         !network.listening &&
                             controller_.state().interface_mode == ManagementInterfaceMode::basic);
  gtk_label_set_text(GTK_LABEL(recovery_message_),
                     network.warning.empty() ? "The Companion listener needs attention."
                                             : network.warning.c_str());
  pages_->show(current_page_, context);
  ui::restore_focus(window_, content_, focus);
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
  // Do not propagate the wide page's minimum through the window: that prevents reaching
  // the breakpoint which rebuilds the page as a narrow layout. Overflow remains reachable.
  gtk_scrolled_window_set_policy(
      GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(content_), scroll);
  pages_ = std::make_unique<LinuxManagementPagesHost>(GTK_SCROLLED_WINDOW(scroll), version_);
  auto* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_add_css_class(footer, "dispatch-footer");
  footer_status_ = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(footer_status_), 0.0F);
  gtk_label_set_ellipsize(GTK_LABEL(footer_status_), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_set_hexpand(footer_status_, TRUE);
  gtk_box_append(GTK_BOX(footer), footer_status_);
  gtk_box_append(GTK_BOX(footer), gtk_label_new(version_.c_str()));
  gtk_box_append(GTK_BOX(content_), footer);
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
  if (narrow_window_)
  {
    navigation_expanded_ = false;
    apply_navigation_layout();
  }
  close_pairing_preview();
  redraw();
}

void LinuxManagementWindow::rebuild_navigation()
{
  for (const auto& [page, button] : navigation_buttons_)
  {
    gtk_widget_set_visible(button,
                           management_page_visible(controller_.state().interface_mode, page));
    if (page == current_page_)
      gtk_widget_add_css_class(button, "selected");
    else
      gtk_widget_remove_css_class(button, "selected");
  }
}

void LinuxManagementWindow::create_navigation()
{
  auto* brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class(brand, "navigation-brand");
  auto* logo = gtk_image_new_from_resource("/com/clownfishweb/PaneLiveCompanion/icon-32.png");
  gtk_image_set_pixel_size(GTK_IMAGE(logo), 30);
  gtk_widget_set_tooltip_text(logo, "PaneLive Companion");
  gtk_box_append(GTK_BOX(brand), logo);
  auto* brand_name = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
  auto* title = gtk_label_new("PaneLive");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0F);
  gtk_widget_add_css_class(title, "navigation-title");
  auto* subtitle = gtk_label_new("COMPANION");
  gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0F);
  gtk_widget_add_css_class(subtitle, "navigation-subtitle");
  gtk_box_append(GTK_BOX(brand_name), title);
  gtk_box_append(GTK_BOX(brand_name), subtitle);
  navigation_labels_.push_back(brand_name);
  gtk_box_append(GTK_BOX(brand), brand_name);
  gtk_box_append(GTK_BOX(navigation_), brand);
  navigation_toggle_ = ui::button("",
                                  [this]
                                  {
                                    navigation_expanded_ = !navigation_expanded_;
                                    if (!narrow_window_)
                                      wide_navigation_expanded_ = navigation_expanded_;
                                    apply_navigation_layout();
                                  });
  gtk_widget_add_css_class(navigation_toggle_, "flat");
  auto* toggle_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  auto* toggle_icon = ui::dispatch_icon("collapse");
  auto* toggle_label = gtk_label_new("Collapse menu");
  gtk_box_append(GTK_BOX(toggle_content), toggle_icon);
  gtk_box_append(GTK_BOX(toggle_content), toggle_label);
  gtk_button_set_child(GTK_BUTTON(navigation_toggle_), toggle_content);
  navigation_labels_.push_back(toggle_label);
  gtk_box_append(GTK_BOX(navigation_), navigation_toggle_);
  auto* primary = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  auto* footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  for (const auto& item : navigation_items)
  {
    auto* button = ui::button("", [this, page = item.page] { select_page(page); });
    navigation_buttons_.emplace(item.page, button);
    gtk_widget_set_tooltip_text(button, std::string(item.label).c_str());
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, std::string(item.label).c_str(), -1);
    if (item.page == ManagementPage::clients)
      gtk_widget_set_name(button, "navigation-clients");
    gtk_widget_set_halign(button, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(button, "flat");
    if (item.page == current_page_)
      gtk_widget_add_css_class(button, "selected");
    auto* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    auto* icon = ui::dispatch_icon(item.icon);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 18);
    gtk_widget_set_size_request(icon, 24, 20);
    auto* label = gtk_label_new(std::string(item.label).c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0F);
    gtk_widget_set_hexpand(label, TRUE);
    navigation_labels_.push_back(label);
    gtk_box_append(GTK_BOX(content), icon);
    gtk_box_append(GTK_BOX(content), label);
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_box_append(GTK_BOX(item.footer ? footer : primary), button);
  }
  gtk_box_append(GTK_BOX(navigation_), primary);
  auto* stores = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top(stores, 18);
  gtk_widget_set_margin_bottom(stores, 12);
  const auto append_banner = [stores](const char* title, const char* url,
                                     const char* caption = "View on")
  {
    auto* banner = gtk_link_button_new(url);
    gtk_widget_add_css_class(banner, "client-store-banner");
    gtk_widget_set_sensitive(banner, *url != '\0');
    const auto accessible_label = std::string(caption) + " " + title;
    gtk_widget_set_tooltip_text(banner, accessible_label.c_str());
    gtk_accessible_update_property(GTK_ACCESSIBLE(banner), GTK_ACCESSIBLE_PROPERTY_LABEL,
                                   accessible_label.c_str(), -1);
    auto* content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    auto* icon = gtk_image_new_from_resource("/com/clownfishweb/PaneLiveCompanion/icon-128.png");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_box_append(GTK_BOX(content), icon);
    auto* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    auto* action = gtk_label_new(*url ? caption : "Coming soon on");
    gtk_label_set_xalign(GTK_LABEL(action), 0.0F);
    gtk_widget_add_css_class(action, "client-store-action");
    auto* name = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(name), 0.0F);
    gtk_widget_add_css_class(name, "client-store-name");
    gtk_box_append(GTK_BOX(text), action);
    gtk_box_append(GTK_BOX(text), name);
    gtk_box_append(GTK_BOX(content), text);
    gtk_button_set_child(GTK_BUTTON(banner), content);
    gtk_box_append(GTK_BOX(stores), banner);
  };
  append_banner("PaneLive Stack", "https://panelive.app", "Learn more about");
  auto* stores_title = gtk_label_new("Get PaneLive Client");
  gtk_label_set_xalign(GTK_LABEL(stores_title), 0.0F);
  gtk_widget_set_margin_top(stores_title, 10);
  gtk_box_append(GTK_BOX(stores), stores_title);
  append_banner("App Store", client_stores::app_store_url);
  append_banner("Google Play", client_stores::play_store_url);
  navigation_labels_.push_back(stores);
  gtk_box_append(GTK_BOX(navigation_), stores);
  auto* spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(spacer, TRUE);
  gtk_box_append(GTK_BOX(navigation_), spacer);
  auto* separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(navigation_), separator);
  gtk_box_append(GTK_BOX(navigation_), footer);
  apply_navigation_layout();
}

void LinuxManagementWindow::apply_navigation_layout() noexcept
{
  const int width = navigation_expanded_ ? 224 : 60;
  gtk_widget_set_size_request(navigation_scroll_, width, -1);
  gtk_widget_set_size_request(navigation_space_, narrow_window_ ? 60 : width, -1);
  for (auto* label : navigation_labels_)
    gtk_widget_set_visible(label, navigation_expanded_);
  if (navigation_expanded_)
    gtk_widget_remove_css_class(navigation_, "compact");
  else
    gtk_widget_add_css_class(navigation_, "compact");
  const auto* label = navigation_expanded_ ? "Collapse navigation" : "Expand navigation";
  auto* toggle_content = gtk_button_get_child(GTK_BUTTON(navigation_toggle_));
  gtk_image_set_from_icon_name(GTK_IMAGE(gtk_widget_get_first_child(toggle_content)),
                               navigation_expanded_ ? "dispatch-collapse-symbolic"
                                                    : "dispatch-expand-symbolic");
  gtk_label_set_text(GTK_LABEL(gtk_widget_get_last_child(toggle_content)),
                     navigation_expanded_ ? "Collapse menu" : "Expand menu");
  gtk_widget_set_tooltip_text(navigation_toggle_, label);
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(navigation_toggle_), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
  gtk_accessible_update_state(
      GTK_ACCESSIBLE(navigation_toggle_), GTK_ACCESSIBLE_STATE_EXPANDED, navigation_expanded_, -1);
  gtk_widget_set_sensitive(content_,
                           !pairing_preview_source_ && !(narrow_window_ && navigation_expanded_));
  queue_layout_redraw();
}

} // namespace studio_remote::linux
