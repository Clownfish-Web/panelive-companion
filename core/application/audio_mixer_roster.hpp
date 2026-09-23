#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <studio_remote/audio/audio_mixer_roster_source.hpp>
#include <studio_remote/common/types.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace studio_remote
{

struct AudioMixerRosterSnapshot
{
  bool observed = false;
  std::unordered_map<std::string, AudioMixerMember> inputs_by_uuid;
};

class AudioMixerRoster
{
public:
  explicit AudioMixerRoster(IAudioMixerRosterSource* source = nullptr);

  bool refresh();
  bool replace(std::vector<AudioMixerMember> members);
  bool observe_telemetry(const std::vector<std::string>& input_names);
  AudioMixerRosterSnapshot wait_for_first_observation(std::chrono::milliseconds timeout) const;
  nlohmann::json diagnostics() const;

private:
  mutable std::mutex mutex_;
  mutable std::condition_variable observation_available_;
  IAudioMixerRosterSource* source_ = nullptr;
  bool observed_ = false;
  std::uint64_t source_sequence_ = 0;
  std::unordered_map<std::string, AudioMixerMember> inputs_by_uuid_;
  std::unordered_map<std::string, std::string> input_uuid_by_name_;
  std::unordered_set<std::string> telemetry_names_;
  std::unordered_set<std::string> telemetry_uuids_;
  std::vector<nlohmann::json> recent_changes_;
};

} // namespace studio_remote
