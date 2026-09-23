#include "obs_quick_transition_duration_ui.hpp"

#include <obs-frontend-api.h>

#include <QMenu>
#include <QMetaObject>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QVariant>

namespace studio_remote::obs_plugin
{
namespace
{

QSpinBox* find_duration_control(QObject& main_window, std::int32_t transition_id)
{
  const auto controls = main_window.findChildren<QSpinBox*>();
  for (auto* control : controls)
  {
    if (control->property("id").toInt() != transition_id)
      continue;

    auto* menu = qobject_cast<QMenu*>(control->parentWidget());
    auto* button = menu ? qobject_cast<QPushButton*>(menu->parentWidget()) : nullptr;
    if (button && button->property("id").toInt() == transition_id)
      return control;
  }
  return nullptr;
}

} // namespace

QuickTransitionDurationUpdateResult update_quick_transition_duration(std::int32_t transition_id,
                                                                     std::uint32_t duration_ms)
{
  auto* main_window = static_cast<QObject*>(obs_frontend_get_main_window());
  if (!main_window)
    return QuickTransitionDurationUpdateResult::control_not_found;

  auto result = QuickTransitionDurationUpdateResult::control_not_found;
  const auto update = [&]()
  {
    auto* control = find_duration_control(*main_window, transition_id);
    if (!control)
      return;

    control->setValue(static_cast<int>(duration_ms));
    obs_frontend_save();
    result = QuickTransitionDurationUpdateResult::updated;
  };

  const auto connection = QThread::currentThread() == main_window->thread()
                              ? Qt::DirectConnection
                              : Qt::BlockingQueuedConnection;
  if (!QMetaObject::invokeMethod(main_window, update, connection))
    return QuickTransitionDurationUpdateResult::invocation_failed;
  return result;
}

} // namespace studio_remote::obs_plugin
