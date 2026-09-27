#include "windows_obs_quick_transition_control.hpp"

#include "../ipc/windows_ipc_mapping.hpp"
#include <studio_remote/obs/obs_quick_transition_protocol.hpp>

#include <stdexcept>
#include <string>
#include <thread>

namespace studio_remote::windows
{
namespace
{

class MappingView
{
public:
  MappingView()
  {
    constexpr DWORD access = FILE_MAP_READ | FILE_MAP_WRITE;
    handle_ = open_owner_restricted_mapping(access, ipc::quick_transition_control_mapping_name);
    if (handle_)
      mapping_ = static_cast<ipc::QuickTransitionControlMapping*>(
          MapViewOfFile(handle_.get(), access, 0, 0, sizeof(ipc::QuickTransitionControlMapping)));
  }

  ~MappingView()
  {
    if (mapping_ && !UnmapViewOfFile(mapping_))
      OutputDebugStringW(L"Could not unmap OBS quick-transition control\n");
  }

  MappingView(const MappingView&) = delete;
  MappingView& operator=(const MappingView&) = delete;

  explicit operator bool() const noexcept
  {
    return handle_ && mapping_;
  }

  ipc::QuickTransitionControlMapping& mapping() const
  {
    return *mapping_;
  }

private:
  IpcMappingHandle handle_;
  ipc::QuickTransitionControlMapping* mapping_ = nullptr;
};

} // namespace

void WindowsObsQuickTransitionControl::trigger(std::int32_t transition_id,
                                               std::chrono::milliseconds timeout) const
{
  submit(ipc::QuickTransitionCommandKind::trigger, transition_id, 0, timeout);
}

void WindowsObsQuickTransitionControl::configure_duration(std::int32_t transition_id,
                                                          std::uint32_t duration_ms,
                                                          std::chrono::milliseconds timeout) const
{
  submit(ipc::QuickTransitionCommandKind::set_duration, transition_id, duration_ms, timeout);
}

void WindowsObsQuickTransitionControl::submit(ipc::QuickTransitionCommandKind kind,
                                              std::int32_t transition_id,
                                              std::uint32_t duration_ms,
                                              std::chrono::milliseconds timeout) const
{
  std::lock_guard command_lock(mutex_);
  MappingView view;
  if (!view)
    throw std::runtime_error("The OBS quick-transition helper is unavailable");
  auto& mapping = view.mapping();
  submit_obs_quick_transition(mapping,
                              kind,
                              transition_id,
                              duration_ms,
                              timeout,
                              {std::chrono::steady_clock::now,
                               [](std::chrono::milliseconds delay)
                               { std::this_thread::sleep_for(delay); }});
}

std::optional<std::vector<ObservedQuickTransition>>
WindowsObsQuickTransitionControl::catalog() const
{
  std::lock_guard command_lock(mutex_);
  MappingView view;
  if (!view)
    return std::nullopt;
  auto& mapping = view.mapping();
  return read_obs_quick_transition_catalog(
      mapping, [](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); });
}

std::optional<std::vector<ObservedObsHotkeyBinding>>
WindowsObsQuickTransitionControl::hotkey_catalog() const
{
  std::lock_guard command_lock(mutex_);
  MappingView view;
  if (!view)
    return std::nullopt;
  auto& mapping = view.mapping();
  return read_obs_hotkey_binding_catalog(
      mapping, [](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); });
}

} // namespace studio_remote::windows
