#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace studio_remote
{

struct AudioMixerMember
{
  std::string input_uuid;
  std::string input_name;
  std::uint32_t membership_reasons = 0;

  bool operator==(const AudioMixerMember&) const = default;
};

struct AudioMixerRosterObservation
{
  bool available = false;
  std::uint64_t sequence = 0;
  std::vector<AudioMixerMember> members;
};

class IAudioMixerRosterSource
{
public:
  virtual ~IAudioMixerRosterSource() = default;
  virtual AudioMixerRosterObservation read() = 0;
};

} // namespace studio_remote
