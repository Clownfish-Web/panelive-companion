#include "linux_audio_mixer_roster.hpp"

#include <studio_remote/ipc/audio_mixer_roster_mapping.hpp>

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace studio_remote::linux
{
namespace
{
std::string bounded_string(const char* value, std::size_t capacity)
{
  return std::string(value, strnlen(value, capacity));
}

class ReadOnlyRosterMapping
{
public:
  explicit ReadOnlyRosterMapping(std::string_view name)
  {
    descriptor_ = shm_open(std::string(name).c_str(), O_RDONLY | O_CLOEXEC, 0);
    struct stat status
    {
    };
    if (descriptor_ < 0 || fstat(descriptor_, &status) != 0 || status.st_uid != geteuid() ||
        (status.st_mode & 0077) != 0 ||
        status.st_size < static_cast<off_t>(sizeof(ipc::AudioMixerRosterMapping)))
    {
      return;
    }
    auto* address =
        mmap(nullptr, sizeof(ipc::AudioMixerRosterMapping), PROT_READ, MAP_SHARED, descriptor_, 0);
    if (address != MAP_FAILED)
      mapping_ = static_cast<const ipc::AudioMixerRosterMapping*>(address);
  }

  ~ReadOnlyRosterMapping()
  {
    if (mapping_)
      munmap(const_cast<ipc::AudioMixerRosterMapping*>(mapping_),
             sizeof(ipc::AudioMixerRosterMapping));
    if (descriptor_ >= 0)
      close(descriptor_);
  }

  ReadOnlyRosterMapping(const ReadOnlyRosterMapping&) = delete;
  ReadOnlyRosterMapping& operator=(const ReadOnlyRosterMapping&) = delete;

  const ipc::AudioMixerRosterMapping* get() const noexcept
  {
    return mapping_;
  }

private:
  int descriptor_ = -1;
  const ipc::AudioMixerRosterMapping* mapping_ = nullptr;
};
} // namespace

LinuxAudioMixerRoster::LinuxAudioMixerRoster(std::string mapping_name)
    : mapping_name_(std::move(mapping_name))
{
}

AudioMixerRosterObservation LinuxAudioMixerRoster::read()
{
  ReadOnlyRosterMapping owner(mapping_name_);
  const auto* mapping = owner.get();
  if (!mapping || mapping->magic != ipc::audio_mixer_roster_magic ||
      mapping->version != ipc::audio_mixer_roster_version ||
      mapping->structure_size != sizeof(ipc::AudioMixerRosterMapping))
  {
    return {};
  }

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
      return {after != 0, after, std::move(members)};
  }
  return {};
}

} // namespace studio_remote::linux
