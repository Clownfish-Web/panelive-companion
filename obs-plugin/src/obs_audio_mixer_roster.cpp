#include "obs_audio_mixer_roster.hpp"

#ifdef __APPLE__
#include "../../platform/macos/ipc/macos_ipc_names.hpp"
#endif

#include <obs-frontend-api.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <util/config-file.h>
#include <util/platform.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <utility>

namespace studio_remote::obs_plugin
{
namespace
{
constexpr std::uint64_t refresh_interval_ns = 500'000'000ULL;
constexpr int global_audio_source_count = 6;

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

void copy_text(char* target, std::size_t capacity, const char* value)
{
  if (!value)
    return;
  const auto size = std::min(capacity - 1, std::strlen(value));
  std::memcpy(target, value, size);
  target[size] = '\0';
}

bool private_setting(obs_source_t* source, const char* name)
{
  obs_data_t* settings = obs_source_get_private_settings(source);
  const bool value = settings && obs_data_get_bool(settings, name);
  if (settings)
    obs_data_release(settings);
  return value;
}

std::unordered_set<std::string> global_source_uuids()
{
  std::unordered_set<std::string> result;
  for (int index = 1; index <= global_audio_source_count; ++index)
  {
    obs_source_t* source = obs_get_output_source(index);
    if (!source)
      continue;
    const char* uuid = obs_source_get_uuid(source);
    if (uuid && *uuid)
      result.emplace(uuid);
    obs_source_release(source);
  }
  return result;
}

std::unordered_set<std::string> preview_source_uuids()
{
  std::unordered_set<std::string> result;
  if (!obs_frontend_preview_program_mode_active())
    return result;
  obs_source_t* source = obs_frontend_get_current_preview_scene();
  obs_scene_t* scene = source ? obs_scene_from_source(source) : nullptr;
  if (scene)
    obs_scene_enum_items(
        scene,
        [](obs_scene_t*, obs_sceneitem_t* item, void* data)
        {
          if (!obs_sceneitem_visible(item))
            return true;
          obs_source_t* item_source = obs_sceneitem_get_source(item);
          if (!item_source || !(obs_source_get_output_flags(item_source) & OBS_SOURCE_AUDIO) ||
              !obs_source_audio_active(item_source))
            return true;
          const char* uuid = obs_source_get_uuid(item_source);
          if (uuid && *uuid)
            static_cast<std::unordered_set<std::string>*>(data)->emplace(uuid);
          return true;
        },
        &result);
  if (source)
    obs_source_release(source);
  return result;
}
} // namespace

ObsAudioMixerRoster::~ObsAudioMixerRoster()
{
  stop();
}

bool ObsAudioMixerRoster::start()
{
  if (started_)
    return true;
  if (!open_mapping())
    return false;
  obs_frontend_add_event_callback(handle_frontend_event, this);
  frontend_callback_registered_ = true;
  obs_add_main_render_callback(on_main_render, this);
  started_ = true;
  return true;
}

void ObsAudioMixerRoster::stop()
{
  if (started_)
    obs_remove_main_render_callback(on_main_render, this);
  if (frontend_callback_registered_)
    obs_frontend_remove_event_callback(handle_frontend_event, this);
  frontend_callback_registered_ = false;
  started_ = false;
  close_mapping();
}

void ObsAudioMixerRoster::on_main_render(void* context, std::uint32_t, std::uint32_t)
{
  if (context)
    static_cast<ObsAudioMixerRoster*>(context)->refresh();
}

void ObsAudioMixerRoster::handle_frontend_event(obs_frontend_event event, void* context)
{
  if (!context || event != OBS_FRONTEND_EVENT_EXIT)
    return;
  auto& roster = *static_cast<ObsAudioMixerRoster*>(context);
  roster.frontend_exited_ = true;
  // OBS destroys the frontend callback list after its EXIT notification and before module
  // unload, so a later removal would only log a warning.
  roster.frontend_callback_registered_ = false;
}

void ObsAudioMixerRoster::refresh()
{
  if (frontend_exited_)
    return;
  const auto now = os_gettime_ns();
  if (now - last_refresh_ns_ < refresh_interval_ns)
    return;
  last_refresh_ns_ = now;

  const auto globals = global_source_uuids();
  const auto previews = preview_source_uuids();
  config_t* config = obs_frontend_get_user_config();
  const bool show_inactive = config && config_get_bool(config, "BasicWindow", "MixerShowInactive");
  const bool show_hidden = config && config_get_bool(config, "BasicWindow", "MixerShowHidden");

  struct Candidate
  {
    obs_source_t* source = nullptr;
    std::string uuid;
  };
  std::vector<Candidate> candidates;
  std::unordered_set<std::string> existing;
  obs_enum_sources(
      [](void* data, obs_source_t* source)
      {
        if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
          return true;
        const char* uuid = obs_source_get_uuid(source);
        if (!uuid || !*uuid)
          return true;
        if (auto* retained = obs_source_get_ref(source))
          static_cast<std::vector<Candidate>*>(data)->push_back({retained, uuid});
        return true;
      },
      &candidates);

  std::vector<ipc::AudioMixerRosterEntry> entries;
  for (const auto& candidate : candidates)
  {
    existing.insert(candidate.uuid);
    const bool audio_active = obs_source_audio_active(candidate.source);
    if (audio_active)
      known_control_uuids_.insert(candidate.uuid);
    if (!known_control_uuids_.contains(candidate.uuid))
      continue;

    const bool active = obs_source_active(candidate.source) && audio_active;
    const bool global = globals.contains(candidate.uuid);
    const bool preview = previews.contains(candidate.uuid);
    const bool pinned = private_setting(candidate.source, "mixer_pinned");
    const bool hidden = private_setting(candidate.source, "mixer_hidden");
    const bool visible = pinned || (hidden && show_hidden) ||
                         (!active && show_inactive && !hidden) || (active && !hidden) ||
                         (preview && !hidden);
    if (!visible)
      continue;

    ipc::AudioMixerRosterEntry entry;
    copy_text(entry.input_uuid, sizeof(entry.input_uuid), candidate.uuid.c_str());
    copy_text(entry.input_name, sizeof(entry.input_name), obs_source_get_name(candidate.source));
    if (active)
      entry.membership_reasons |= ipc::audio_mixer_program;
    if (global)
      entry.membership_reasons |= ipc::audio_mixer_global;
    if (preview)
      entry.membership_reasons |= ipc::audio_mixer_preview;
    if (pinned)
      entry.membership_reasons |= ipc::audio_mixer_pinned;
    if (!active && show_inactive)
      entry.membership_reasons |= ipc::audio_mixer_inactive_visible;
    if (hidden && show_hidden)
      entry.membership_reasons |= ipc::audio_mixer_hidden_visible;
    entries.push_back(entry);
  }
  for (const auto& candidate : candidates)
    obs_source_release(candidate.source);
  std::erase_if(known_control_uuids_, [&](const auto& uuid) { return !existing.contains(uuid); });
  std::sort(entries.begin(),
            entries.end(),
            [](const auto& left, const auto& right)
            { return std::strcmp(left.input_uuid, right.input_uuid) < 0; });
  publish(entries);
}

bool ObsAudioMixerRoster::open_mapping()
{
#ifdef _WIN32
  if (mapping_)
    return true;
  auto memory = windows::SharedMemoryOwner::create(ipc::audio_mixer_roster_mapping_name,
                                                   sizeof(ipc::AudioMixerRosterMapping));
  if (!memory)
    return false;
  mapping_ = reinterpret_cast<ipc::AudioMixerRosterMapping*>(memory->bytes().data());
  mapping_owner_ = std::move(memory);
#elif defined(__APPLE__)
  mapping_owner_ = macos::SharedMemoryOwner::create(
      studio_remote::macos::ipc_names::audio_mixer_roster, sizeof(ipc::AudioMixerRosterMapping));
  if (!mapping_owner_)
    return false;
  mapping_ = reinterpret_cast<ipc::AudioMixerRosterMapping*>(mapping_owner_->bytes().data());
#else
  mapping_ = create_mapping<ipc::AudioMixerRosterMapping>(
      ipc::audio_mixer_roster_shared_memory_name, mapping_handle_);
  if (!mapping_)
    return false;
#endif
  std::construct_at(mapping_);
  mapping_->structure_size = sizeof(ipc::AudioMixerRosterMapping);
#ifdef _WIN32
  mapping_->producer_process_id = GetCurrentProcessId();
#else
  mapping_->producer_process_id = static_cast<std::uint32_t>(getpid());
#endif
  return true;
}

void ObsAudioMixerRoster::close_mapping()
{
#ifdef _WIN32
  mapping_ = nullptr;
  mapping_owner_.reset();
#elif defined(__APPLE__)
  mapping_ = nullptr;
  mapping_owner_.reset();
#else
  if (mapping_)
    munmap(mapping_, sizeof(ipc::AudioMixerRosterMapping));
  if (mapping_handle_ >= 0)
    close(mapping_handle_);
  mapping_ = nullptr;
  mapping_handle_ = -1;
#endif
}

void ObsAudioMixerRoster::publish(const std::vector<ipc::AudioMixerRosterEntry>& entries)
{
  if (!mapping_)
    return;
  const auto count = std::min(entries.size(), ipc::maximum_audio_mixer_entries);
  const auto writing = sequence_ + 1;
  mapping_->sequence.store(writing, std::memory_order_release);
  mapping_->entry_count = static_cast<std::uint32_t>(count);
  std::copy_n(entries.begin(), count, mapping_->entries);
  sequence_ = writing + 1;
  mapping_->sequence.store(sequence_, std::memory_order_release);
}

} // namespace studio_remote::obs_plugin
