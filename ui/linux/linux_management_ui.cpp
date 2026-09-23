#include "linux_management_ui.hpp"

#include "linux_management_window.hpp"
#include "linux_status_notifier_item.hpp"

#include "../../platform/linux/platform_services/linux_process.hpp"

#include <adwaita.h>
#include <atomic>
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

constexpr char stylesheet[] = R"css(
.navigation-sidebar { background: alpha(@window_fg_color, 0.025); padding: 20px 12px 12px; }
.navigation-brand { margin: 0 12px 14px; }
.navigation-title { font-size: 20px; font-weight: 700; }
.navigation-sidebar button.selected { background: alpha(@window_fg_color, 0.12); }
.card { border-radius: 8px; }
.attached-script { border-left: 2px solid #57d68d; background: alpha(#57d68d, 0.08); }
.attached-script-icon { color: #57d68d; }
.unattached-script-info { opacity: 0.72; }
button.script-detach { background: #b42330; color: white; }
button.script-attach { background: #205caa; color: white; }
.pill { border-radius: 999px; padding: 4px 10px; font-size: 0.85em; }
.pill.neutral { background: alpha(@window_fg_color, 0.10); }
.pill.healthy { color: #57d68d; background: alpha(#1c7d46, 0.30); }
.pill.warning { color: #ffd36a; background: alpha(#9c7500, 0.30); }
.pill.error { color: #ff9b98; background: alpha(#b3262e, 0.30); }
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
                 ShutdownSignal& shutdown)
      : companion_name_(std::move(companion_name)), version_(std::move(version)),
        initial_page_(initial_management_page(
            interface_mode, obs_connection_configured, network_recovery_required)),
        interface_mode_(interface_mode), show_on_start_(show_on_start),
        interface_mode_changed_(std::move(interface_mode_changed)),
        callbacks_(std::move(callbacks)), shutdown_(shutdown)
  {
    linux_management_resources_register_resource();
    application_ = adw_application_new(application_id, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application_, "activate", G_CALLBACK(activate), this);
  }

  ~Implementation()
  {
    accepting_requests_ = false;
    if (refresh_source_ != 0)
      g_source_remove(refresh_source_);
    window_.reset();
    controller_.reset();
    g_clear_object(&application_);
    linux_management_resources_unregister_resource();
  }

  int run()
  {
    char application_name[] = "panelive-companion";
    char* arguments[] = {application_name, nullptr};
    return g_application_run(G_APPLICATION(application_), 1, arguments);
  }

  bool request_role_change(ManagementRoleChangeRequest request, ManagementApprovalDecision decision)
  {
    const auto key = std::pair{request.device_id, request.request_id};
    {
      std::lock_guard lock(request_mutex_);
      if (!accepting_requests_)
        return false;
      if (!pending_role_requests_.insert(key).second)
        return true;
    }
    dispatch(
        [this, request = std::move(request), decision = std::move(decision), key]() mutable
        {
          window_->request_role_change(
              std::move(request),
              [this, decision = std::move(decision), key](bool approved) mutable
              {
                complete_approval(decision, approved, "Could not complete the client role request");
                std::lock_guard lock(request_mutex_);
                pending_role_requests_.erase(key);
              });
        });
    return true;
  }

  bool request_global_setting_change(ManagementGlobalSettingChangeRequest request,
                                     ManagementApprovalDecision decision)
  {
    const auto key = std::pair{request.device_id, request.request_id};
    {
      std::lock_guard lock(request_mutex_);
      if (!accepting_requests_)
        return false;
      if (!pending_global_requests_.insert(key).second)
        return true;
    }
    dispatch(
        [this, request = std::move(request), decision = std::move(decision), key]() mutable
        {
          window_->request_global_setting_change(
              std::move(request),
              [this, decision = std::move(decision), key](bool approved) mutable
              {
                complete_approval(
                    decision, approved, "Could not complete the global setting request");
                std::lock_guard lock(request_mutex_);
                pending_global_requests_.erase(key);
              });
        });
    return true;
  }

private:
  static gboolean run_dispatched(gpointer data)
  {
    std::unique_ptr<std::function<void()>> action(static_cast<std::function<void()>*>(data));
    (*action)();
    return G_SOURCE_REMOVE;
  }

  void dispatch(std::function<void()> action)
  {
    auto* source = g_idle_source_new();
    g_source_set_callback(
        source, run_dispatched, new std::function<void()>(std::move(action)), nullptr);
    g_source_attach(source, g_main_context_default());
    g_source_unref(source);
  }

  void complete_approval(const ManagementApprovalDecision& decision,
                         bool approved,
                         std::string_view failure)
  {
    try
    {
      decision(approved);
    }
    catch (...)
    {
      controller_->report_error(std::string(failure));
      window_->redraw();
    }
  }

  static void activate(GApplication*, gpointer context)
  {
    static_cast<Implementation*>(context)->on_activate();
  }

  static gboolean refresh(gpointer context)
  {
    return static_cast<Implementation*>(context)->on_refresh();
  }

  void on_activate()
  {
    if (window_)
    {
      window_->activate();
      return;
    }
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(),
                                       ADW_COLOR_SCHEME_FORCE_DARK);
    install_styles();
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
    accepting_requests_ = true;
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
      accepting_requests_ = false;
      refresh_source_ = 0;
      if (window_)
        window_->close();
      g_application_release(G_APPLICATION(application_));
      g_application_quit(G_APPLICATION(application_));
      return G_SOURCE_REMOVE;
    }

    if (!gtk_widget_get_visible(GTK_WIDGET(window_->window())) || ++refresh_ticks_ < 10)
      return G_SOURCE_CONTINUE;
    refresh_ticks_ = 0;
    controller_->refresh();
    const auto page = window_->current_page();
    if (page != ManagementPage::advanced_settings)
      window_->redraw();
    return G_SOURCE_CONTINUE;
  }

  std::string companion_name_;
  std::string version_;
  ManagementPage initial_page_ = ManagementPage::clients;
  ManagementInterfaceMode interface_mode_ = ManagementInterfaceMode::basic;
  bool show_on_start_ = false;
  std::function<void(ManagementInterfaceMode)> interface_mode_changed_;
  ManagementUiCallbacks callbacks_;
  ShutdownSignal& shutdown_;
  AdwApplication* application_ = nullptr;
  std::unique_ptr<ManagementUiController> controller_;
  std::unique_ptr<LinuxManagementWindow> window_;
  std::unique_ptr<LinuxStatusNotifierItem> status_item_;
  guint refresh_source_ = 0;
  std::uint32_t refresh_ticks_ = 0;
  std::atomic<bool> accepting_requests_{false};
  std::mutex request_mutex_;
  std::set<std::pair<std::string, std::string>> pending_role_requests_;
  std::set<std::pair<std::string, std::string>> pending_global_requests_;
};

bool activate_existing_management_ui()
{
  auto* application = g_application_new(application_id, G_APPLICATION_DEFAULT_FLAGS);
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
    ShutdownSignal& shutdown)
    : implementation_(std::make_unique<Implementation>(std::move(companion_name),
                                                       std::move(version),
                                                       obs_connection_configured,
                                                       network_recovery_required,
                                                       show_on_start,
                                                       interface_mode,
                                                       std::move(interface_mode_changed),
                                                       std::move(callbacks),
                                                       shutdown))
{
}

LinuxManagementUi::~LinuxManagementUi() = default;

int LinuxManagementUi::run()
{
  return implementation_->run();
}

bool LinuxManagementUi::request_role_change(ManagementRoleChangeRequest request,
                                            ManagementApprovalDecision decision)
{
  return implementation_->request_role_change(std::move(request), std::move(decision));
}

bool LinuxManagementUi::request_global_setting_change(ManagementGlobalSettingChangeRequest request,
                                                      ManagementApprovalDecision decision)
{
  return implementation_->request_global_setting_change(std::move(request), std::move(decision));
}
} // namespace studio_remote::linux
