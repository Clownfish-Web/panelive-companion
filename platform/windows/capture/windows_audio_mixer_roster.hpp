#pragma once

#include <studio_remote/audio/audio_mixer_roster_source.hpp>

namespace studio_remote::windows
{

class WindowsAudioMixerRoster final : public IAudioMixerRosterSource
{
public:
  AudioMixerRosterObservation read() override;
};

} // namespace studio_remote::windows
