#pragma once

#include "../ipc/macos_ipc_names.hpp"
#include <string>

#include <studio_remote/audio/audio_mixer_roster_source.hpp>

namespace studio_remote::macos
{

class MacosAudioMixerRoster final : public IAudioMixerRosterSource
{
public:
  explicit MacosAudioMixerRoster(std::string mapping_name = ipc_names::audio_mixer_roster);
  AudioMixerRosterObservation read() override;

private:
  const std::string mapping_name_;
};

} // namespace studio_remote::macos
