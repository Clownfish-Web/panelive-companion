#include "windows_audio_mixer_roster.hpp"

#include <studio_remote/ipc/audio_mixer_roster_mapping.hpp>

#include "../ipc/windows_ipc_mapping.hpp"

#include <algorithm>
#include <cstring>

namespace studio_remote::windows
{
namespace
{
std::string bounded_string(const char* value, std::size_t capacity)
{
  return std::string(value, strnlen_s(value, capacity));
}
} // namespace

AudioMixerRosterObservation WindowsAudioMixerRoster::read()
{
  const auto handle =
      open_owner_restricted_mapping(FILE_MAP_READ, ipc::audio_mixer_roster_mapping_name);
  if (!handle)
    return {};
  const IpcMappingView view(
      MapViewOfFile(handle.get(), FILE_MAP_READ, 0, 0, sizeof(ipc::AudioMixerRosterMapping)));
  if (!view)
    return {};
  const auto* mapping = static_cast<const ipc::AudioMixerRosterMapping*>(view.get());

  AudioMixerRosterObservation observation;
  if (mapping->magic == ipc::audio_mixer_roster_magic &&
      mapping->version == ipc::audio_mixer_roster_version &&
      mapping->structure_size == sizeof(ipc::AudioMixerRosterMapping))
  {
    for (int attempt = 0; attempt < 3; ++attempt)
    {
      const auto before = mapping->sequence.load(std::memory_order_acquire);
      if ((before & 1U) != 0)
        continue;
      const auto count =
          std::min<std::size_t>(mapping->entry_count, ipc::maximum_audio_mixer_entries);
      std::vector<AudioMixerMember> members;
      members.reserve(count);
      for (std::size_t index = 0; index < count; ++index)
      {
        const auto& entry = mapping->entries[index];
        members.push_back({bounded_string(entry.input_uuid, sizeof(entry.input_uuid)),
                           bounded_string(entry.input_name, sizeof(entry.input_name)),
                           entry.membership_reasons});
      }
      const auto after = mapping->sequence.load(std::memory_order_acquire);
      if (before == after && (after & 1U) == 0)
      {
        observation = {after != 0, after, std::move(members)};
        break;
      }
    }
  }
  return observation;
}

} // namespace studio_remote::windows
