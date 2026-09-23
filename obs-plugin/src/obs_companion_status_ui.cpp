#include "obs_companion_status_ui.hpp"

#include "obs_frame_capture.hpp"
#include "obs_frontend_symbols.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include <QAction>
#include <QBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstring>
#include <mutex>
#include <optional>
#include <utility>

namespace studio_remote::obs_plugin
{
namespace
{

constexpr char dock_id[] = "panelive-companion-obs-plugin.status";
using AddDock = bool (*)(const char*, const char*, void*);
using RemoveDock = void (*)(const char*);

std::uint64_t monotonic_time_ns()
{
  // Use the same native time domain as OBS frame publication and Companion demand heartbeats.
  return os_gettime_ns();
}

template <typename Function> Function frontend_function(const char* name)
{
  const auto address = obs_frontend_symbol(name);
  Function function = nullptr;
  static_assert(sizeof(function) == sizeof(address));
  std::memcpy(&function, &address, sizeof(function));
  return function;
}

QString state_text(CompanionCaptureState state)
{
  switch (state)
  {
  case CompanionCaptureState::starting:
    return QStringLiteral("Starting");
  case CompanionCaptureState::active:
    return QStringLiteral("Active");
  case CompanionCaptureState::stopping:
    return QStringLiteral("Stopping");
  case CompanionCaptureState::faulted:
    return QStringLiteral("Error");
  case CompanionCaptureState::idle:
    return QStringLiteral("Idle");
  }
  return QStringLiteral("Idle");
}

QString rate_text(std::uint32_t fps)
{
  return fps == 0 ? QStringLiteral("Off") : QStringLiteral("%1 FPS").arg(fps);
}

} // namespace

class ObsCompanionStatusUi::Implementation
{
public:
  bool start(ObsFrameCapture& capture)
  {
    if (callback_registered_)
      return true;
    capture_ = &capture;
    obs_frontend_add_event_callback(handle_frontend_event, this);
    callback_registered_ = true;
    return true;
  }

  void stop()
  {
    if (callback_registered_)
      obs_frontend_remove_event_callback(handle_frontend_event, this);
    callback_registered_ = false;
    capture_ = nullptr;
    if (!context_)
      return;
    timer_->stop();
    if (dock_registered_)
    {
      if (const auto remove_dock = frontend_function<RemoveDock>("obs_frontend_remove_dock"))
        remove_dock(dock_id);
      dock_registered_ = false;
    }
    if (indicator_)
    {
      if (auto* layout = qobject_cast<QBoxLayout*>(indicator_->parentWidget()->layout()))
        layout->removeWidget(indicator_);
      indicator_->deleteLater();
    }
    if (fallback_action_)
      fallback_action_->deleteLater();
    auto* context = context_.data();
    context_.clear();
    timer_.clear();
    dock_content_.clear();
    indicator_.clear();
    fallback_action_.clear();
    delete context;
  }

  void post_status(CompanionStatusSnapshot snapshot)
  {
    if (!context_)
      return;
    {
      std::lock_guard lock(snapshot_mutex_);
      if (latest_snapshot_ && latest_snapshot_->generation >= snapshot.generation)
        return;
      latest_snapshot_ = std::move(snapshot);
      if (update_queued_)
        return;
      update_queued_ = true;
    }
    QMetaObject::invokeMethod(
        context_.data(), [this] { apply_latest_status(); }, Qt::QueuedConnection);
  }

private:
  static void handle_frontend_event(obs_frontend_event event, void* context)
  {
    if (!context)
      return;
    auto& ui = *static_cast<Implementation*>(context);
    switch (event)
    {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING:
      ui.frontend_finished_loading();
      break;
    case OBS_FRONTEND_EVENT_EXIT:
      if (ui.timer_)
        ui.timer_->stop();
      // OBS clears the frontend callback list after dispatching EXIT.
      ui.callback_registered_ = false;
      break;
    default:
      break;
    }
  }

  void frontend_finished_loading()
  {
    if (context_ || !capture_)
      return;
    auto* main_window = static_cast<QWidget*>(obs_frontend_get_main_window());
    if (!main_window)
    {
      blog(LOG_WARNING, "[PaneLive Companion] OBS main window unavailable after startup");
      return;
    }

    context_ = new QObject(main_window);
    create_indicator(*main_window);
    create_status_surface();
    timer_ = new QTimer(context_);
    timer_->setInterval(100);
    QObject::connect(timer_,
                     &QTimer::timeout,
                     context_,
                     [this]
                     {
                       if (capture_)
                         capture_->poll_demand(monotonic_time_ns());
                     });
    timer_->start();
  }

  void create_indicator(QWidget& main_window)
  {
    QList<QPushButton*> candidates;
    for (auto* button : main_window.findChildren<QPushButton*>(QStringLiteral("settingsButton")))
      if (button->isEnabled())
        candidates.push_back(button);
    if (candidates.size() != 1)
    {
      blog(LOG_WARNING,
           "[PaneLive Companion] OBS Controls status indicator unavailable: settings control "
           "lookup");
      return;
    }

    auto* settings = candidates.front();
    auto* layout = settings->parentWidget()
                       ? qobject_cast<QBoxLayout*>(settings->parentWidget()->layout())
                       : nullptr;
    const auto index = layout ? layout->indexOf(settings) : -1;
    if (!layout || index < 0)
    {
      blog(LOG_WARNING,
           "[PaneLive Companion] OBS Controls status indicator unavailable: incompatible layout");
      return;
    }

    auto* indicator = new QPushButton(settings->parentWidget());
    indicator->setEnabled(false);
    indicator->setAccessibleName(QStringLiteral("PaneLive output status"));
    indicator->setAccessibleDescription(
        QStringLiteral("Read-only status of the PaneLive Companion OBS output"));
    indicator->hide();
    layout->insertWidget(index + 1, indicator);
    indicator_ = indicator;
  }

  void create_status_surface()
  {
    auto* content = new QWidget();
    content->setAccessibleName(QStringLiteral("PaneLive Companion status"));
    content->setAccessibleDescription(
        QStringLiteral("Read-only Companion media client and OBS output status"));
    auto* layout = new QVBoxLayout(content);
    state_label_ = create_label(*layout, "PaneLive output state");
    clients_label_ = create_label(*layout, "Active Companion video clients");
    program_label_ = create_label(*layout, "Requested Program frame rate");
    preview_label_ = create_label(*layout, "Requested Preview frame rate");
    health_label_ = create_label(*layout, "Companion output health");
    layout->addStretch();
    dock_content_ = content;

    const auto add_dock = frontend_function<AddDock>("obs_frontend_add_dock_by_id");
    if (add_dock && add_dock(dock_id, obs_module_text("PaneLiveCompanion.DockTitle"), content))
    {
      dock_registered_ = true;
      return;
    }

    content->deleteLater();
    dock_content_.clear();
    auto* action = static_cast<QAction*>(
        obs_frontend_add_tools_menu_qaction(obs_module_text("PaneLiveCompanion.ToolsStatus")));
    if (action)
    {
      action->setEnabled(false);
      fallback_action_ = action;
      blog(LOG_WARNING,
           "[PaneLive Companion] OBS dock API unavailable; using Tools-menu status fallback");
    }
  }

  QLabel* create_label(QVBoxLayout& layout, const char* accessible_name)
  {
    auto* label = new QLabel();
    label->setAccessibleName(QString::fromUtf8(accessible_name));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout.addWidget(label);
    return label;
  }

  void apply_latest_status()
  {
    std::optional<CompanionStatusSnapshot> snapshot;
    {
      std::lock_guard lock(snapshot_mutex_);
      snapshot = latest_snapshot_;
      update_queued_ = false;
    }
    if (!snapshot || snapshot->generation <= displayed_generation_)
      return;
    displayed_generation_ = snapshot->generation;

    const auto output_state = state_text(snapshot->capture_state);
    if (state_label_)
      state_label_->setText(QStringLiteral("Output: %1").arg(output_state));
    if (clients_label_)
      clients_label_->setText(
          QStringLiteral("Active clients: %1").arg(snapshot->active_video_client_count));
    const bool show_rates = snapshot->capture_state == CompanionCaptureState::starting ||
                            snapshot->capture_state == CompanionCaptureState::active;
    if (program_label_)
    {
      program_label_->setText(
          QStringLiteral("Program: %1").arg(rate_text(snapshot->requested_program_fps)));
      program_label_->setVisible(show_rates);
    }
    if (preview_label_)
    {
      preview_label_->setText(
          QStringLiteral("Preview: %1").arg(rate_text(snapshot->requested_preview_fps)));
      preview_label_->setVisible(show_rates);
    }
    if (health_label_)
    {
      const bool unhealthy = !snapshot->companion_heartbeat_fresh ||
                             snapshot->capture_state == CompanionCaptureState::faulted;
      health_label_->setText(snapshot->capture_state == CompanionCaptureState::faulted
                                 ? QStringLiteral("Output could not start; waiting for new demand")
                                 : QStringLiteral("Companion demand heartbeat is stale"));
      health_label_->setVisible(unhealthy);
    }
    update_indicator(*snapshot);
    if (fallback_action_)
      fallback_action_->setText(QStringLiteral("PaneLive Companion - Output: %1, clients: %2")
                                    .arg(output_state)
                                    .arg(snapshot->active_video_client_count));
  }

  void update_indicator(const CompanionStatusSnapshot& snapshot)
  {
    if (!indicator_)
      return;
    switch (snapshot.capture_state)
    {
    case CompanionCaptureState::starting:
      indicator_->setText(QStringLiteral("PaneLive output starting..."));
      indicator_->show();
      break;
    case CompanionCaptureState::active:
      indicator_->setText(QStringLiteral("PaneLive output active"));
      indicator_->setProperty("activeOutput", true);
      indicator_->show();
      break;
    case CompanionCaptureState::stopping:
      indicator_->setText(QStringLiteral("PaneLive output stopping..."));
      indicator_->show();
      break;
    case CompanionCaptureState::faulted:
    case CompanionCaptureState::idle:
      indicator_->setProperty("activeOutput", false);
      indicator_->hide();
      break;
    }
  }

  QPointer<QObject> context_;
  QPointer<QTimer> timer_;
  QPointer<QWidget> dock_content_;
  QPointer<QPushButton> indicator_;
  QPointer<QAction> fallback_action_;
  QPointer<QLabel> state_label_;
  QPointer<QLabel> clients_label_;
  QPointer<QLabel> program_label_;
  QPointer<QLabel> preview_label_;
  QPointer<QLabel> health_label_;
  ObsFrameCapture* capture_ = nullptr;
  std::mutex snapshot_mutex_;
  std::optional<CompanionStatusSnapshot> latest_snapshot_;
  std::uint64_t displayed_generation_ = 0;
  bool update_queued_ = false;
  bool dock_registered_ = false;
  bool callback_registered_ = false;
};

ObsCompanionStatusUi::ObsCompanionStatusUi() : implementation_(std::make_unique<Implementation>())
{
}

ObsCompanionStatusUi::~ObsCompanionStatusUi()
{
  stop();
}

bool ObsCompanionStatusUi::start(ObsFrameCapture& capture)
{
  return implementation_->start(capture);
}

void ObsCompanionStatusUi::stop()
{
  implementation_->stop();
}

void ObsCompanionStatusUi::post_status(CompanionStatusSnapshot snapshot)
{
  implementation_->post_status(std::move(snapshot));
}

} // namespace studio_remote::obs_plugin
