#include "obs_quick_transition_control.hpp"

#ifdef __APPLE__
#include "../../platform/macos/ipc/macos_ipc_names.hpp"
#endif
#include "obs_quick_transition_duration_ui.hpp"

#include <obs.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <charconv>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace studio_remote::obs_plugin
{
namespace
{

#if !defined(_WIN32) && !defined(__APPLE__)
template <typename Mapping> Mapping* create_mapping(const char* name, int& descriptor)
{
  const auto candidate = shm_open(name, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (candidate < 0)
    return nullptr;
  struct stat status{};
  if (fstat(candidate, &status) != 0 || status.st_uid != geteuid() ||
      fchmod(candidate, 0600) != 0 ||
      ftruncate(candidate, static_cast<off_t>(sizeof(Mapping))) != 0)
  {
    close(candidate);
    return nullptr;
  }
  auto* address = mmap(nullptr, sizeof(Mapping), PROT_READ | PROT_WRITE, MAP_SHARED, candidate, 0);
  if (address == MAP_FAILED)
  {
    close(candidate);
    return nullptr;
  }
  descriptor = candidate;
  return static_cast<Mapping*>(address);
}
#endif

struct HotkeySearch
{
  std::int32_t transition_id = 0;
  obs_hotkey_id hotkey_id = OBS_INVALID_HOTKEY_ID;
};

struct CatalogEntry
{
  std::int32_t transition_id = 0;
  std::string label;
};

struct CatalogSnapshot
{
  std::vector<CatalogEntry> entries;
};

struct HotkeyBindingEntry
{
  std::string identifier;
  std::string label;
  std::string key_identifier;
  std::uint32_t modifiers = 0;
};

struct HotkeyBindingSnapshot
{
  std::vector<HotkeyBindingEntry> entries;
};

std::optional<std::int32_t> quick_transition_id(std::string_view name)
{
  constexpr std::string_view prefix = "OBSBasic.QuickTransition.";
  if (!name.starts_with(prefix))
    return std::nullopt;
  std::int32_t transition_id = 0;
  const auto suffix = name.substr(prefix.size());
  const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), transition_id);
  if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size())
    return std::nullopt;
  return transition_id;
}

bool find_quick_transition_hotkey(void* context, obs_hotkey_id id, obs_hotkey_t* hotkey)
{
  const auto transition_id = quick_transition_id(obs_hotkey_get_name(hotkey));
  if (!transition_id)
    return true;
  auto& search = *static_cast<HotkeySearch*>(context);
  if (*transition_id == search.transition_id)
  {
    search.hotkey_id = id;
    return false;
  }
  return true;
}

bool collect_quick_transition_hotkey(void* context, obs_hotkey_id, obs_hotkey_t* hotkey)
{
  const auto transition_id = quick_transition_id(obs_hotkey_get_name(hotkey));
  if (!transition_id)
    return true;

  const char* description = obs_hotkey_get_description(hotkey);
  if (!description || !*description)
    return true;

  auto& snapshot = *static_cast<CatalogSnapshot*>(context);
  if (snapshot.entries.size() >= ipc::max_quick_transition_catalog_entries)
    return false;
  snapshot.entries.push_back({*transition_id, description});
  return true;
}

bool collect_hotkey_binding(void* context, std::size_t, obs_hotkey_binding_t* binding)
{
  auto& snapshot = *static_cast<HotkeyBindingSnapshot*>(context);
  if (snapshot.entries.size() >= ipc::max_obs_hotkey_binding_entries)
    return false;

  const auto combination = obs_hotkey_binding_get_key_combination(binding);
  if (combination.key == OBS_KEY_NONE)
    return true;

  auto* hotkey = obs_hotkey_binding_get_hotkey(binding);
  if (!hotkey)
    return true;
  const char* identifier = obs_hotkey_get_name(hotkey);
  if (!identifier || !*identifier)
    return true;
  const char* description = obs_hotkey_get_description(hotkey);
  const char* key_identifier = obs_key_to_name(combination.key);
  if (!key_identifier || !*key_identifier)
    return true;

  std::uint32_t modifiers = 0;
  if ((combination.modifiers & INTERACT_SHIFT_KEY) != 0)
    modifiers |= ipc::obs_hotkey_modifier_shift;
  if ((combination.modifiers & INTERACT_CONTROL_KEY) != 0)
    modifiers |= ipc::obs_hotkey_modifier_control;
  if ((combination.modifiers & INTERACT_ALT_KEY) != 0)
    modifiers |= ipc::obs_hotkey_modifier_alt;
  if ((combination.modifiers & INTERACT_COMMAND_KEY) != 0)
    modifiers |= ipc::obs_hotkey_modifier_command;

  snapshot.entries.push_back({identifier,
                              description && *description ? description : identifier,
                              key_identifier,
                              modifiers});
  return true;
}

} // namespace

ObsQuickTransitionControl::~ObsQuickTransitionControl()
{
  stop();
}

bool ObsQuickTransitionControl::start()
{
  if (!open_mapping())
    return false;
  obs_add_main_render_callback(&ObsQuickTransitionControl::on_main_render, this);
  callback_registered_ = true;
  return true;
}

void ObsQuickTransitionControl::stop()
{
  if (callback_registered_)
    obs_remove_main_render_callback(&ObsQuickTransitionControl::on_main_render, this);
  callback_registered_ = false;
  close_mapping();
}

void ObsQuickTransitionControl::on_main_render(void* context, std::uint32_t, std::uint32_t)
{
  auto& control = *static_cast<ObsQuickTransitionControl*>(context);
  control.publish_catalog();
  control.process_command();
}

void ObsQuickTransitionControl::process_command()
{
  if (!mapping_)
    return;
  const auto sequence = mapping_->command_sequence.load(std::memory_order_acquire);
  if (sequence == 0 || sequence == processed_sequence_)
    return;
  processed_sequence_ = sequence;

  HotkeySearch search{mapping_->requested_transition_id.load(std::memory_order_acquire)};
  obs_enum_hotkeys(&find_quick_transition_hotkey, &search);
  auto status = ipc::QuickTransitionCommandStatus::transition_not_found;
  if (search.hotkey_id != OBS_INVALID_HOTKEY_ID)
  {
    const auto kind = static_cast<ipc::QuickTransitionCommandKind>(
        mapping_->requested_command_kind.load(std::memory_order_acquire));
    if (kind == ipc::QuickTransitionCommandKind::trigger)
    {
      obs_hotkey_trigger_routed_callback(search.hotkey_id, true);
      obs_hotkey_trigger_routed_callback(search.hotkey_id, false);
      status = ipc::QuickTransitionCommandStatus::completed;
    }
    else if (kind == ipc::QuickTransitionCommandKind::set_duration)
    {
      const auto duration = mapping_->requested_duration_ms.load(std::memory_order_acquire);
      switch (update_quick_transition_duration(search.transition_id, duration))
      {
      case QuickTransitionDurationUpdateResult::updated:
        configured_durations_[search.transition_id] = duration;
        status = ipc::QuickTransitionCommandStatus::completed;
        break;
      case QuickTransitionDurationUpdateResult::control_not_found:
        status = ipc::QuickTransitionCommandStatus::duration_control_not_found;
        break;
      case QuickTransitionDurationUpdateResult::invocation_failed:
        status = ipc::QuickTransitionCommandStatus::ui_invocation_failed;
        break;
      }
    }
  }
  mapping_->command_status.store(static_cast<std::int32_t>(status), std::memory_order_release);
  mapping_->completed_sequence.store(sequence, std::memory_order_release);
}

void ObsQuickTransitionControl::publish_catalog()
{
  if (!mapping_)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (now < next_catalog_publish_)
    return;
  next_catalog_publish_ = now + std::chrono::milliseconds(500);

  CatalogSnapshot snapshot;
  obs_enum_hotkeys(&collect_quick_transition_hotkey, &snapshot);
  std::sort(snapshot.entries.begin(),
            snapshot.entries.end(),
            [](const CatalogEntry& left, const CatalogEntry& right)
            { return left.transition_id < right.transition_id; });

  const auto sequence = catalog_sequence_ + 1;
  mapping_->catalog_sequence.store(sequence, std::memory_order_release);
  mapping_->catalog_count.store(static_cast<std::uint32_t>(snapshot.entries.size()),
                                std::memory_order_release);

  for (std::size_t index = 0; index < mapping_->catalog.size(); ++index)
  {
    auto& target = mapping_->catalog[index];
    target.transition_id = 0;
    target.duration_ms = 0;
    target.label.fill('\0');
    if (index >= snapshot.entries.size())
      continue;

    const auto& source = snapshot.entries[index];
    target.transition_id = source.transition_id;
    if (const auto duration = configured_durations_.find(source.transition_id);
        duration != configured_durations_.end())
      target.duration_ms = duration->second;
    std::strncpy(target.label.data(), source.label.c_str(), target.label.size() - 1);
  }

  catalog_sequence_ = sequence + 1;
  mapping_->catalog_sequence.store(catalog_sequence_, std::memory_order_release);

  HotkeyBindingSnapshot hotkey_snapshot;
  obs_enum_hotkey_bindings(&collect_hotkey_binding, &hotkey_snapshot);
  const auto hotkey_sequence = hotkey_catalog_sequence_ + 1;
  mapping_->hotkey_catalog_sequence.store(hotkey_sequence, std::memory_order_release);
  mapping_->hotkey_catalog_count.store(static_cast<std::uint32_t>(hotkey_snapshot.entries.size()),
                                       std::memory_order_release);

  for (std::size_t index = 0; index < mapping_->hotkey_catalog.size(); ++index)
  {
    auto& target = mapping_->hotkey_catalog[index];
    target = {};
    if (index >= hotkey_snapshot.entries.size())
      continue;

    const auto& source = hotkey_snapshot.entries[index];
    std::strncpy(target.identifier.data(), source.identifier.c_str(), target.identifier.size() - 1);
    std::strncpy(target.label.data(), source.label.c_str(), target.label.size() - 1);
    std::strncpy(target.key_identifier.data(),
                 source.key_identifier.c_str(),
                 target.key_identifier.size() - 1);
    target.modifiers = source.modifiers;
  }

  hotkey_catalog_sequence_ = hotkey_sequence + 1;
  mapping_->hotkey_catalog_sequence.store(hotkey_catalog_sequence_, std::memory_order_release);
}

bool ObsQuickTransitionControl::open_mapping()
{
#ifdef _WIN32
  if (mapping_)
    return true;
  auto memory = windows::SharedMemoryOwner::create(ipc::quick_transition_control_mapping_name,
                                                   sizeof(ipc::QuickTransitionControlMapping));
  if (!memory)
    return false;
  mapping_ = reinterpret_cast<ipc::QuickTransitionControlMapping*>(memory->bytes().data());
  mapping_owner_ = std::move(memory);
#elif defined(__APPLE__)
  mapping_owner_ =
      macos::SharedMemoryOwner::create(studio_remote::macos::ipc_names::quick_transition_control,
                                       sizeof(ipc::QuickTransitionControlMapping));
  if (!mapping_owner_)
    return false;
  mapping_ = reinterpret_cast<ipc::QuickTransitionControlMapping*>(mapping_owner_->bytes().data());
#else
  mapping_ = create_mapping<ipc::QuickTransitionControlMapping>(
      ipc::quick_transition_control_shared_memory_name, mapping_handle_);
  if (!mapping_)
    return false;
#endif
  std::construct_at(mapping_);
  mapping_->structure_size = sizeof(ipc::QuickTransitionControlMapping);
#ifdef _WIN32
  mapping_->producer_process_id = GetCurrentProcessId();
#else
  mapping_->producer_process_id = static_cast<std::uint32_t>(getpid());
#endif
  return true;
}

void ObsQuickTransitionControl::close_mapping()
{
#ifdef _WIN32
  mapping_ = nullptr;
  mapping_owner_.reset();
#elif defined(__APPLE__)
  mapping_ = nullptr;
  mapping_owner_.reset();
#else
  if (mapping_)
    munmap(mapping_, sizeof(ipc::QuickTransitionControlMapping));
  if (mapping_handle_ >= 0)
    close(mapping_handle_);
  mapping_ = nullptr;
  mapping_handle_ = -1;
#endif
}

} // namespace studio_remote::obs_plugin
