#include "linux_management_ui.hpp"

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
.navigation-sidebar { background: alpha(@window_fg_color, 0.025); padding: 20px 12px 12px; }
.navigation-brand { margin: 0 12px 14px; }
.navigation-title { font-size: 20px; font-weight: 700; }
.navigation-sidebar button.selected { background: alpha(@window_fg_color, 0.12); }
.card { border-radius: 8px; }
.pairing-preview-backdrop { background: rgba(0, 0, 0, 0.65); }
.pairing-preview-panel { background: @window_bg_color; border-radius: 8px; padding: 6px; }
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
    status_item_.reset();
    if (window_ && window_->window())
      window_->close();
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
      approvals_->close();
      stop_dispatch();
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
