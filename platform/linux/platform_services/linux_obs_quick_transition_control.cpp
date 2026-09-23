#include "linux_obs_quick_transition_control.hpp"

#include <algorithm>
#include <array>
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace studio_remote::linux
{
namespace
{
template <std::size_t Size> std::string bounded_string(const std::array<char, Size>& value)
{
  const auto end = std::find(value.begin(), value.end(), '\0');
  return std::string(value.begin(), end);
}

class MappingView
{
public:
  explicit MappingView(const std::string& name)
  {
    descriptor_ = shm_open(name.c_str(), O_RDWR | O_CLOEXEC, 0);
    struct stat status
    {
    };
    if (descriptor_ < 0 || fstat(descriptor_, &status) != 0 || status.st_uid != geteuid() ||
        (status.st_mode & 0077) != 0 ||
        status.st_size < static_cast<off_t>(sizeof(ipc::QuickTransitionControlMapping)))
    {
      return;
    }
    auto* address = mmap(nullptr,
                         sizeof(ipc::QuickTransitionControlMapping),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED,
                         descriptor_,
                         0);
    if (address != MAP_FAILED)
      mapping_ = static_cast<ipc::QuickTransitionControlMapping*>(address);
  }

  ~MappingView()
  {
    if (mapping_)
      munmap(mapping_, sizeof(ipc::QuickTransitionControlMapping));
    if (descriptor_ >= 0)
      close(descriptor_);
  }

  explicit operator bool() const noexcept
  {
    return mapping_ != nullptr;
  }

  ipc::QuickTransitionControlMapping& mapping() const
  {
    return *mapping_;
  }

private:
  int descriptor_ = -1;
  ipc::QuickTransitionControlMapping* mapping_ = nullptr;
};
} // namespace

LinuxObsQuickTransitionControl::LinuxObsQuickTransitionControl(std::string mapping_name)
    : mapping_name_(std::move(mapping_name))
{
}

void LinuxObsQuickTransitionControl::trigger(std::int32_t transition_id,
                                             std::chrono::milliseconds timeout) const
{
  submit(ipc::QuickTransitionCommandKind::trigger, transition_id, 0, timeout);
}

void LinuxObsQuickTransitionControl::configure_duration(std::int32_t transition_id,
                                                        std::uint32_t duration_ms,
                                                        std::chrono::milliseconds timeout) const
{
  submit(ipc::QuickTransitionCommandKind::set_duration, transition_id, duration_ms, timeout);
}

void LinuxObsQuickTransitionControl::submit(ipc::QuickTransitionCommandKind kind,
                                            std::int32_t transition_id,
                                            std::uint32_t duration_ms,
                                            std::chrono::milliseconds timeout) const
{
  std::lock_guard command_lock(mutex_);
  MappingView view(mapping_name_);
  if (!view)
    throw std::runtime_error("The OBS quick-transition helper is unavailable");
  auto& mapping = view.mapping();
  if (!ipc::compatible_quick_transition_control(mapping))
    throw std::runtime_error("The OBS quick-transition helper version is incompatible");

  const auto sequence = mapping.command_sequence.load(std::memory_order_acquire) + 1;
  mapping.requested_command_kind.store(static_cast<std::int32_t>(kind), std::memory_order_release);
  mapping.requested_transition_id.store(transition_id, std::memory_order_release);
  mapping.requested_duration_ms.store(duration_ms, std::memory_order_release);
  mapping.command_status.store(
      static_cast<std::int32_t>(ipc::QuickTransitionCommandStatus::pending),
      std::memory_order_release);
  mapping.command_sequence.store(sequence, std::memory_order_release);

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (mapping.completed_sequence.load(std::memory_order_acquire) != sequence &&
         std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

  const bool completed = mapping.completed_sequence.load(std::memory_order_acquire) == sequence;
  const auto status = static_cast<ipc::QuickTransitionCommandStatus>(
      mapping.command_status.load(std::memory_order_acquire));
  if (!completed)
    throw std::runtime_error("OBS quick-transition helper timed out");
  if (status == ipc::QuickTransitionCommandStatus::duration_control_not_found)
    throw std::runtime_error(
        "OBS quick-transition duration control is unavailable; enable Studio Mode");
  if (status == ipc::QuickTransitionCommandStatus::ui_invocation_failed)
    throw std::runtime_error("OBS rejected the quick-transition UI update");
  if (status != ipc::QuickTransitionCommandStatus::completed)
    throw std::runtime_error("OBS quick-transition button is no longer available");
}

std::optional<std::vector<ObservedQuickTransition>> LinuxObsQuickTransitionControl::catalog() const
{
  std::lock_guard command_lock(mutex_);
  MappingView view(mapping_name_);
  if (!view || !ipc::compatible_quick_transition_control(view.mapping()))
    return std::nullopt;
  auto& mapping = view.mapping();

  for (int attempt = 0; attempt < 3; ++attempt)
  {
    const auto before = mapping.catalog_sequence.load(std::memory_order_acquire);
    if ((before & 1U) != 0)
      continue;
    const auto count =
        std::min<std::uint32_t>(mapping.catalog_count.load(std::memory_order_acquire),
                                ipc::max_quick_transition_catalog_entries);
    std::vector<ObservedQuickTransition> entries;
    entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const auto& source = mapping.catalog[index];
      if (source.transition_id > 0)
        entries.push_back({source.transition_id, source.duration_ms, bounded_string(source.label)});
    }
    const auto after = mapping.catalog_sequence.load(std::memory_order_acquire);
    if (before == after && (after & 1U) == 0)
      return entries;
  }
  return std::nullopt;
}

std::optional<std::vector<ObservedObsHotkeyBinding>>
LinuxObsQuickTransitionControl::hotkey_catalog() const
{
  std::lock_guard command_lock(mutex_);
  MappingView view(mapping_name_);
  if (!view || !ipc::compatible_quick_transition_control(view.mapping()))
    return std::nullopt;
  auto& mapping = view.mapping();

  for (int attempt = 0; attempt < 3; ++attempt)
  {
    const auto before = mapping.hotkey_catalog_sequence.load(std::memory_order_acquire);
    if ((before & 1U) != 0)
      continue;
    const auto count =
        std::min<std::uint32_t>(mapping.hotkey_catalog_count.load(std::memory_order_acquire),
                                ipc::max_obs_hotkey_binding_entries);
    std::vector<ObservedObsHotkeyBinding> entries;
    entries.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
      const auto& source = mapping.hotkey_catalog[index];
      auto identifier = bounded_string(source.identifier);
      auto label = bounded_string(source.label);
      auto key = bounded_string(source.key_identifier);
      if (!identifier.empty() && !label.empty() && !key.empty())
        entries.push_back(
            {std::move(identifier), std::move(label), std::move(key), source.modifiers});
    }
    const auto after = mapping.hotkey_catalog_sequence.load(std::memory_order_acquire);
    if (before == after && (after & 1U) == 0)
      return entries;
  }
  return std::nullopt;
}

} // namespace studio_remote::linux
