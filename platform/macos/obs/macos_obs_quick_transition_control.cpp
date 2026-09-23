#include "macos_obs_quick_transition_control.hpp"

#include "../ipc/writable_shared_memory.hpp"
#include <studio_remote/obs/obs_quick_transition_protocol.hpp>

#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace studio_remote::macos
{
MacosObsQuickTransitionControl::MacosObsQuickTransitionControl(std::string mapping_name)
    : mapping_name_(std::move(mapping_name))
{
}

void MacosObsQuickTransitionControl::trigger(std::int32_t transition_id,
                                             std::chrono::milliseconds timeout) const
{
  submit(ipc::QuickTransitionCommandKind::trigger, transition_id, 0, timeout);
}

void MacosObsQuickTransitionControl::configure_duration(std::int32_t transition_id,
                                                        std::uint32_t duration_ms,
                                                        std::chrono::milliseconds timeout) const
{
  submit(ipc::QuickTransitionCommandKind::set_duration, transition_id, duration_ms, timeout);
}

void MacosObsQuickTransitionControl::submit(ipc::QuickTransitionCommandKind kind,
                                            std::int32_t transition_id,
                                            std::uint32_t duration_ms,
                                            std::chrono::milliseconds timeout) const
{
  std::lock_guard command_lock(mutex_);
  auto view = WritableSharedMemory::open(mapping_name_, sizeof(ipc::QuickTransitionControlMapping));
  if (!view)
    throw std::runtime_error("The OBS quick-transition helper is unavailable");
  auto& mapping = *reinterpret_cast<ipc::QuickTransitionControlMapping*>(view->bytes().data());
  submit_obs_quick_transition(mapping,
                              kind,
                              transition_id,
                              duration_ms,
                              timeout,
                              {std::chrono::steady_clock::now,
                               [](std::chrono::milliseconds delay)
                               { std::this_thread::sleep_for(delay); }});
}

std::optional<std::vector<ObservedQuickTransition>> MacosObsQuickTransitionControl::catalog() const
{
  std::lock_guard command_lock(mutex_);
  auto view = WritableSharedMemory::open(mapping_name_, sizeof(ipc::QuickTransitionControlMapping));
  if (!view)
    return std::nullopt;
  auto& mapping = *reinterpret_cast<ipc::QuickTransitionControlMapping*>(view->bytes().data());
  return read_obs_quick_transition_catalog(
      mapping, [](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); });
}

std::optional<std::vector<ObservedObsHotkeyBinding>>
MacosObsQuickTransitionControl::hotkey_catalog() const
{
  std::lock_guard command_lock(mutex_);
  auto view = WritableSharedMemory::open(mapping_name_, sizeof(ipc::QuickTransitionControlMapping));
  if (!view)
    return std::nullopt;
  auto& mapping = *reinterpret_cast<ipc::QuickTransitionControlMapping*>(view->bytes().data());
  return read_obs_hotkey_binding_catalog(
      mapping, [](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); });
}

} // namespace studio_remote::macos
