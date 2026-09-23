#pragma once

#include <studio_remote/audio/audio_mixer_roster_source.hpp>
#include <studio_remote/ipc/audio_mixer_roster_mapping.hpp>

#include <string>

namespace studio_remote::linux
{

class LinuxAudioMixerRoster final : public IAudioMixerRosterSource
{
public:
  explicit LinuxAudioMixerRoster(
      std::string mapping_name = ipc::audio_mixer_roster_shared_memory_name);
  AudioMixerRosterObservation read() override;

private:
  std::string mapping_name_;
};

} // namespace studio_remote::linux
