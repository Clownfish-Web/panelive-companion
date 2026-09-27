#include "linux_management_ui.hpp"

#include "linux_notification_delivery.hpp"

#include "linux_appearance.hpp"
#include "linux_management_window.hpp"
#include "linux_status_notifier_item.hpp"

#include "../../platform/linux/platform_services/linux_process.hpp"

#include <adwaita.h>
#include <chrono>
#include <gtk/gtk.h>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>

extern "C" void linux_management_resources_register_resource();
extern "C" void linux_management_resources_unregister_resource();

namespace studio_remote::linux
{
namespace
{
constexpr char application_id[] = "com.clownfishweb.PaneLiveCompanion";
constexpr auto application_flags = static_cast<GApplicationFlags>(0);

constexpr char stylesheet[] = R"css(
@define-color panelive_control_bg @view_bg_color;
@define-color panelive_muted_fg alpha(@window_fg_color, 0.6);
/* Keep accent and status text readable without an Omarchy palette override. */
@define-color panelive_selection_bg alpha(@accent_bg_color, 0.15);
@define-color panelive_selection_fg @window_fg_color;
window { color: @window_fg_color; background-color: @window_bg_color; }
.dim-label { color: @panelive_muted_fg; opacity: 1; }
/* Form surfaces must follow the same palette as the page, including native popups. */
entry, spinbutton, textview, textview text {
  color: @window_fg_color; background-color: @panelive_control_bg; caret-color: @window_fg_color;
}
entry, spinbutton {
  border: 1px solid @borders; border-radius: 0; box-shadow: none;
}
entry:focus-within, spinbutton:focus-within {
  border-color: @accent_color; outline: 2px solid alpha(@accent_color, 0.6); outline-offset: 1px;
}
entry text, spinbutton text { background: transparent; color: inherit; }
entry placeholder { color: @panelive_muted_fg; }
entry.error, spinbutton.error { color: @error_color; border-color: @error_color; }
entry.warning, spinbutton.warning { color: @warning_color; border-color: @warning_color; }
textview { border: 1px solid @borders; border-radius: 0; padding: 8px; }
textview:focus-within { border-color: @accent_color; }
selection { background-color: @panelive_selection_bg; color: @panelive_selection_fg; }
button {
  background: @panelive_control_bg; color: @window_fg_color;
  border: 1px solid @borders; border-radius: 0; box-shadow: none;
}
button:hover, button:active, button:checked {
  background: @panelive_selection_bg; color: @panelive_selection_fg;
}
button.suggested-action { background: @accent_bg_color; color: @accent_fg_color; }
button.destructive-action { background: @destructive_bg_color; color: @destructive_fg_color; }
button:focus-visible, switch:focus-visible, checkbutton:focus-visible,
flowboxchild:focus-visible, listview row:focus-visible, scale:focus-visible {
  outline: 2px solid @accent_color; outline-offset: 2px;
}
entry:disabled, spinbutton:disabled, button:disabled, switch:disabled, checkbutton:disabled {
  opacity: 0.5;
}
spinbutton > button { border: 0; border-left: 1px solid @borders; }
dropdown > button { background: @panelive_control_bg; color: @window_fg_color; }
popover > contents, popover listview, popover row, menu {
  background: @popover_bg_color; color: @popover_fg_color;
}
popover > contents { border: 1px solid @borders; border-radius: 0; }
popover row, popover listview, dropdown > button { border-radius: 0; }
messagedialog, messagedialog .dialog-surface, dialog, window.dialog {
  background: @window_bg_color; color: @window_fg_color; border-radius: 0;
}
popover row:hover { background: @panelive_selection_bg; color: @panelive_selection_fg; }
popover row:selected { background: @panelive_selection_bg; color: @panelive_selection_fg; }
switch { background: @panelive_control_bg; border: 1px solid @borders;
  border-radius: 0; }
switch:checked { background: @accent_bg_color; border-color: @accent_bg_color; }
switch slider { background: @window_fg_color; border: 0; border-radius: 0; box-shadow: none; }
switch:checked slider { background: @accent_fg_color; }
checkbutton check, checkbutton radio {
  background: @view_bg_color; color: @window_fg_color;
  border: 1px solid @borders; box-shadow: none;
}
checkbutton check { border-radius: 0; }
checkbutton check:checked, checkbutton radio:checked,
checkbutton check:indeterminate {
  background: @accent_bg_color; color: @accent_fg_color; border-color: @accent_bg_color;
}
scale trough, progressbar trough {
  background: @panelive_control_bg; border-radius: 0; box-shadow: none;
}
scale highlight, progressbar progress {
  background: @accent_bg_color; border-radius: 0; box-shadow: none;
}
scale slider {
  background: @window_fg_color; border: 1px solid @borders;
  border-radius: 0; box-shadow: none;
}
scrollbar { background: @window_bg_color; }
scrollbar slider {
  background: @panelive_muted_fg; border-radius: 0; border: 0; box-shadow: none;
  min-width: 8px; min-height: 8px; margin: 0;
}
scrollbar slider:hover, scrollbar slider:active { background: @accent_color; }
tooltip, tooltip.background {
  background: @popover_bg_color; color: @popover_fg_color;
  border: 1px solid @borders; border-radius: 0; box-shadow: none;
}
tooltip label { color: @popover_fg_color; }
separator { background: @borders; }
label link, label link:visited { color: @accent_color; }
button.link, button.link:visited { color: @accent_color; }
rubberband { background: alpha(@accent_bg_color, 0.2); border: 1px solid @accent_color; }
.navigation-sidebar { background: @card_bg_color; color: @window_fg_color; padding: 22px 12px 12px;
  border-right: 1px solid @borders; }
.navigation-sidebar.compact { padding: 22px 8px 12px; }
.navigation-sidebar.compact .navigation-brand { margin: 0 6px 20px; }
.navigation-brand { margin: 0 9px 20px; }
.navigation-title { font-size: 20px; font-weight: 600; letter-spacing: -0.8px; }
.navigation-subtitle { font-size: 8px; font-weight: 450; letter-spacing: 1.6px; opacity: 0.8; }
.navigation-sidebar button { min-height: 22px; padding: 10px; font-size: 12px;
  border: 0; background: transparent; box-shadow: none; }
.navigation-sidebar button.selected { background: @panelive_selection_bg;
  color: @accent_color; box-shadow: inset 2px 0 @accent_bg_color; }
.navigation-sidebar button:hover { background: @panelive_selection_bg;
  color: @panelive_selection_fg; }
.card { border-radius: 0; background: transparent; color: @window_fg_color; box-shadow: none;
  border: 1px solid @borders; }
.pairing-preview-backdrop { background: alpha(@window_bg_color, 0.85); }
.pairing-preview-panel { background: @window_bg_color; border-radius: 0; padding: 6px; }
.attached-script { border-left: 2px solid @success_color; background: alpha(@success_color, 0.08); }
.attached-script-icon { color: @success_color; }
.unattached-script-info { opacity: 0.72; }
button.script-detach { background: @destructive_bg_color; color: @destructive_fg_color; }
button.script-attach { background: @accent_bg_color; color: @accent_fg_color; }
.dispatch-page { padding: 32px 40px; }
.dispatch .title-1 { font-size: 60px; font-weight: 450; letter-spacing: -4px; margin: 10px 0; }
.dispatch.compact-layout .title-1 { font-size: 42px; letter-spacing: -2.9px; }
.dispatch.compact-layout .dispatch-page { padding: 24px 16px; }
.dispatch-page-heading { border-bottom: 2px solid @window_fg_color; padding-bottom: 28px;
  margin-bottom: 8px; }
.dispatch-overline { font-size: 10px; letter-spacing: 1.3px; }
.dispatch-page-heading .dim-label { font-size: 12px; }
.dispatch-page-heading .dispatch-overline { font-size: 10px; }
.dispatch-page-actions button { font-size: 12px; padding: 9px 13px; border-radius: 0;
  background: transparent; box-shadow: none; border: 1px solid @borders; }
.dispatch-page-actions button.suggested-action { background: @accent_bg_color;
  color: @accent_fg_color; border-color: @accent_bg_color; }
.dispatch flowboxchild { padding: 0; border-radius: 0; }
.dispatch-client { padding: 22px 12px; background: transparent; border: 0;
  border-bottom: 1px solid @borders; border-radius: 0; box-shadow: none; }
.dispatch-client.selected { background: @panelive_selection_bg;
  color: @panelive_selection_fg;
  box-shadow: inset 2px 0 @accent_bg_color; }
.dispatch-client.selected .dim-label { color: @panelive_selection_fg; opacity: 0.75; }
.dispatch-client .title-3 { font-size: 18px; font-weight: 450; }
.dispatch-number { font-family: monospace; font-size: 11px; color: @accent_color; }
.dispatch-inspector { padding: 22px 0; border-top: 1px solid @borders; }
.dispatch-inspector .title-3 { font-size: 28px; font-weight: 550; }
.dispatch-inspector button { background: transparent; box-shadow: none; border: 0;
  border-bottom: 1px solid @borders; border-radius: 0; padding: 12px 0;
  font-size: 12px; }
.dispatch-connection { font-size: 11px; }
.dispatch-connection.online { color: @success_color; }
.dispatch-connection.offline { opacity: 0.8; }
.dispatch-fact { font-size: 12px; }
.dispatch-toolbar { margin: 4px 0 0; font-size: 11px; }
.dispatch-toolbar button { background: transparent; box-shadow: none;
  border: 1px solid @borders; border-radius: 0; font-size: 12px; }
.dispatch-search { background: transparent; box-shadow: none; border: 0;
  border-bottom: 1px solid @borders; border-radius: 0; padding: 9px 4px;
  font-size: 11px; min-width: 0; }
.dispatch-client-code { font-family: monospace; font-size: 10px; letter-spacing: 0.4px; }
.dispatch-client-facts { font-size: 11px; }
.dispatch-table-heading { padding: 0 12px 12px;
  border-bottom: 1px solid @borders; font-size: 10px; }
.dispatch-inspector.side-inspector { border-top: 0;
  border-left: 1px solid @borders; padding: 20px 0 20px 28px; }
.dispatch-page-actions button:hover, .dispatch-toolbar button:hover,
.dispatch-inspector button:hover { background: @panelive_selection_bg;
  color: @panelive_selection_fg; }
.dispatch-page-actions button.suggested-action:hover { background: @accent_bg_color;
  color: @accent_fg_color; }
.dispatch-footer { padding: 11px 24px; border-top: 1px solid @borders;
  font-size: 10px; }
.pill { border-radius: 0; padding: 4px 10px; font-size: 0.85em; }
.pill.neutral { background: alpha(@window_fg_color, 0.10); }
.pill.healthy { color: @success_color; background: alpha(@success_color, 0.12); }
.pill.warning { color: @warning_color; background: alpha(@warning_color, 0.12); }
.pill.error { color: @error_color; background: alpha(@error_color, 0.12); }
)css";

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
void install_styles()
{
  auto* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, stylesheet, -1);
  gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}
G_GNUC_END_IGNORE_DEPRECATIONS
} // namespace

class LinuxManagementUi::Implementation
{
public:
  Implementation(std::string companion_name,
                 std::string version,
                 bool obs_connection_configured,
                 bool network_recovery_required,
                 bool show_on_start,
                 ManagementInterfaceMode interface_mode,
                 std::function<void(ManagementInterfaceMode)> interface_mode_changed,
                 ManagementUiCallbacks callbacks,
                 ShutdownSignal& shutdown,
                 std::function<void(const RoleChangeRequest&, bool)> decide_role,
                 std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global)
      : companion_name_(std::move(companion_name)), version_(std::move(version)),
        initial_page_(initial_management_page(
            interface_mode, obs_connection_configured, network_recovery_required)),
        interface_mode_(interface_mode), show_on_start_(show_on_start),
        interface_mode_changed_(std::move(interface_mode_changed)),
        callbacks_(std::move(callbacks)), shutdown_(shutdown)
  {
    linux_management_resources_register_resource();
    application_ = adw_application_new(application_id, application_flags);
    g_signal_connect(application_, "activate", G_CALLBACK(activate), this);
    auto* open_action = g_simple_action_new("open-management", nullptr);
    g_signal_connect_swapped(
        open_action, "activate", G_CALLBACK(g_application_activate), application_);
    g_action_map_add_action(G_ACTION_MAP(application_), G_ACTION(open_action));
    g_object_unref(open_action);
    approvals_ = std::make_unique<ManagementApprovalDispatcher>(ManagementApprovalDispatchActions{
        [this](auto action) { return dispatch(std::move(action)); },
        [](auto action)
        {
          action();
          return true;
        },
        [this](ManagementRoleChangeRequest request, ManagementApprovalDecision decision)
        {
          window_->request_role_change(std::move(request), std::move(decision));
          return true;
        },
        [this](ManagementGlobalSettingChangeRequest request, ManagementApprovalDecision decision)
        {
          window_->request_global_setting_change(std::move(request), std::move(decision));
          return true;
        },
        std::move(decide_role),
        std::move(decide_global),
        [this](std::string_view error) noexcept
        {
          try
          {
            if (!dispatch(
                    [this, message = std::string(error)]
                    {
                      report_management_approval_error(*controller_, message);
                      window_->redraw();
                    }))
              g_warning("Could not report a client approval failure");
          }
          catch (...)
          {
            g_warning("Could not report a client approval failure");
          }
        },
        [this](std::string_view device, std::string_view request)
        { window_->cancel_role_change(device, request); },
        [this](std::string_view device, std::string_view request)
        { window_->cancel_global_setting_change(device, request); }});
    pairing_close_->hide = [this]
    {
      dispatch(
          [this]
          {
            if (window_)
              window_->clear_pairing();
          });
    };
  }

  ~Implementation()
  {
    {
      std::lock_guard lock(pairing_close_->mutex);
      pairing_close_->hide = {};
    }
    approvals_->close();
    stop_dispatch();
    if (refresh_source_ != 0)
      g_source_remove(refresh_source_);
    notification_delivery_.reset();
    status_item_.reset();
    if (window_ && window_->window())
      window_->close();
    window_.reset();
    controller_.reset();
    appearance_.reset();
    g_clear_object(&application_);
    linux_management_resources_unregister_resource();
  }

  int run()
  {
    char application_name[] = "panelive-companion";
    char* arguments[] = {application_name, nullptr};
    return g_application_run(G_APPLICATION(application_), 1, arguments);
  }

  ManagementApprovalRequesters approval_requesters() const
  {
    return approvals_->requesters();
  }

  std::function<void()> pairing_closed_handler() const
  {
    return [target = pairing_close_]
    {
      try
      {
        std::lock_guard lock(target->mutex);
        if (target->hide)
          target->hide();
      }
      catch (...)
      {
        g_warning("Could not close pairing presentation");
      }
    };
  }

private:
  struct DispatchedTask
  {
    Implementation* owner;
    std::function<void()> action;
  };

  static gboolean run_dispatched(gpointer data) noexcept
  {
    auto& task = *static_cast<DispatchedTask*>(data);
    {
      std::lock_guard lock(task.owner->dispatch_mutex_);
      task.owner->dispatch_sources_.erase(g_source_get_id(g_main_current_source()));
    }
    try
    {
      task.action();
    }
    catch (...)
    {
      g_warning("Management dispatch failed");
    }
    return G_SOURCE_REMOVE;
  }

  bool dispatch(std::function<void()> action)
  {
    std::lock_guard lock(dispatch_mutex_);
    if (dispatch_stopped_)
      return false;
    auto* source = g_idle_source_new();
    g_source_set_callback(source,
                          run_dispatched,
                          new DispatchedTask{this, std::move(action)},
                          [](gpointer data) { delete static_cast<DispatchedTask*>(data); });
    dispatch_sources_.insert(g_source_attach(source, g_main_context_default()));
    g_source_unref(source);
    return true;
  }

  void stop_dispatch()
  {
    std::set<guint> sources;
    {
      std::lock_guard lock(dispatch_mutex_);
      dispatch_stopped_ = true;
      sources.swap(dispatch_sources_);
    }
    for (const auto source : sources)
      g_source_remove(source);
  }

  static void activate(GApplication*, gpointer context)
  {
    static_cast<Implementation*>(context)->on_activate();
  }

  static gboolean refresh(gpointer context)
  {
    try
    {
      return static_cast<Implementation*>(context)->on_refresh();
    }
    catch (const std::exception& error)
    {
      g_warning("Companion desktop refresh failed: %s", error.what());
    }
    catch (...)
    {
      g_warning("Companion desktop refresh failed");
    }
    return G_SOURCE_CONTINUE;
  }

  void on_activate()
  {
    if (stopping_)
      return;
    if (window_)
    {
      window_->activate();
      return;
    }
    install_styles();
    appearance_ = std::make_unique<LinuxAppearance>();
    controller_ = std::make_unique<ManagementUiController>(
        interface_mode_, initial_page_, std::move(interface_mode_changed_), std::move(callbacks_));
    controller_->refresh();
    window_ = std::make_unique<LinuxManagementWindow>(
        application_, companion_name_, version_, *controller_);
    status_item_ = std::make_unique<LinuxStatusNotifierItem>(
        LinuxStatusNotifierActions{[this] { window_->activate(); },
                                   [this]
                                   {
                                     controller_->pair_client();
                                     window_->redraw();
                                     window_->activate();
                                   },
                                   [this](ManagementInterfaceMode mode)
                                   {
                                     controller_->set_interface_mode(mode);
                                     window_->redraw();
                                   },
                                   [this] { return controller_->state().interface_mode; },
                                   [this] { controller_->shutdown(); }});
    status_item_->update(make_linux_desktop_status(controller_->state()));
    notification_delivery_ =
        std::make_unique<LinuxNotificationDelivery>(G_APPLICATION(application_));
    client_notifications_.observe(controller_->state().clients);
    g_application_hold(G_APPLICATION(application_));
    refresh_source_ = g_timeout_add(500, refresh, this);
    if (show_on_start_)
      window_->activate();
  }

  gboolean on_refresh()
  {
    bool stopping = false;
    try
    {
      stopping = shutdown_.wait_for(std::chrono::milliseconds(0));
    }
    catch (...)
    {
      stopping = true;
    }
    if (stopping)
    {
      stopping_ = true;
      approvals_->close();
      stop_dispatch();
      status_item_.reset();
      refresh_source_ = 0;
      if (window_)
        window_->close();
      g_application_release(G_APPLICATION(application_));
      g_application_quit(G_APPLICATION(application_));
      return G_SOURCE_REMOVE;
    }

    if (++refresh_ticks_ < 10)
      return G_SOURCE_CONTINUE;
    refresh_ticks_ = 0;
    controller_->refresh();
    status_item_->update(make_linux_desktop_status(controller_->state()));
    if (const auto notice = client_notifications_.observe(controller_->state().clients))
      notification_delivery_->show(*notice);
    if (gtk_widget_get_visible(GTK_WIDGET(window_->window())))
      window_->redraw();
    return G_SOURCE_CONTINUE;
  }

  std::string companion_name_;
  std::string version_;
  ManagementPage initial_page_ = ManagementPage::clients;
  ManagementInterfaceMode interface_mode_ = ManagementInterfaceMode::basic;
  bool show_on_start_ = false;
  bool stopping_ = false;
  std::function<void(ManagementInterfaceMode)> interface_mode_changed_;
  ManagementUiCallbacks callbacks_;
  ShutdownSignal& shutdown_;
  AdwApplication* application_ = nullptr;
  std::unique_ptr<ManagementUiController> controller_;
  std::unique_ptr<LinuxManagementWindow> window_;
  std::unique_ptr<LinuxAppearance> appearance_;
  std::unique_ptr<LinuxStatusNotifierItem> status_item_;
  ManagementClientNotifications client_notifications_;
  std::unique_ptr<LinuxNotificationDelivery> notification_delivery_;
  guint refresh_source_ = 0;
  std::uint32_t refresh_ticks_ = 0;
  std::unique_ptr<ManagementApprovalDispatcher> approvals_;
  std::mutex dispatch_mutex_;
  std::set<guint> dispatch_sources_;
  bool dispatch_stopped_ = false;
  struct PairingCloseTarget
  {
    std::mutex mutex;
    std::function<void()> hide;
  };
  // A retained close callback prolongs only this gate, never the frontend or GTK resources.
  std::shared_ptr<PairingCloseTarget> pairing_close_ = std::make_shared<PairingCloseTarget>();
};

bool activate_existing_management_ui()
{
  auto* application = g_application_new(application_id, application_flags);
  GError* error = nullptr;
  if (!g_application_register(application, nullptr, &error))
  {
    const std::string message = error ? error->message : "unknown D-Bus registration error";
    g_clear_error(&error);
    g_object_unref(application);
    throw std::runtime_error("Could not contact the Linux management application: " + message);
  }
  const bool remote = g_application_get_is_remote(application);
  if (remote)
    g_application_activate(application);
  g_object_unref(application);
  return remote;
}

LinuxManagementUi::LinuxManagementUi(
    std::string companion_name,
    std::string version,
    bool obs_connection_configured,
    bool network_recovery_required,
    bool show_on_start,
    ManagementInterfaceMode interface_mode,
    std::function<void(ManagementInterfaceMode)> interface_mode_changed,
    ManagementUiCallbacks callbacks,
    ShutdownSignal& shutdown,
    std::function<void(const RoleChangeRequest&, bool)> decide_role,
    std::function<void(const GlobalSettingChangeRequest&, bool)> decide_global)
    : implementation_(std::make_unique<Implementation>(std::move(companion_name),
                                                       std::move(version),
                                                       obs_connection_configured,
                                                       network_recovery_required,
                                                       show_on_start,
                                                       interface_mode,
                                                       std::move(interface_mode_changed),
                                                       std::move(callbacks),
                                                       shutdown,
                                                       std::move(decide_role),
                                                       std::move(decide_global)))
{
}

LinuxManagementUi::~LinuxManagementUi() = default;

int LinuxManagementUi::run()
{
  return implementation_->run();
}

ManagementApprovalRequesters LinuxManagementUi::approval_requesters() const
{
  return implementation_->approval_requesters();
}

std::function<void()> LinuxManagementUi::pairing_closed_handler() const
{
  return implementation_->pairing_closed_handler();
}
} // namespace studio_remote::linux
