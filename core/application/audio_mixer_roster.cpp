#include "audio_mixer_roster.hpp"

#include <algorithm>
#include <set>

namespace studio_remote
{
namespace
{
json sorted_values(const std::unordered_set<std::string>& values)
{
  std::set<std::string> sorted(values.begin(), values.end());
  return sorted;
}
} // namespace

AudioMixerRoster::AudioMixerRoster(IAudioMixerRosterSource* source) : source_(source)
{
}

bool AudioMixerRoster::refresh()
{
  if (!source_)
    return false;

  const auto observation = source_->read();
  if (!observation.available)
    return false;

  {
    std::lock_guard lock(mutex_);
    if (observation.sequence != 0 && observation.sequence == source_sequence_)
      return false;
    source_sequence_ = observation.sequence;
  }
  return replace(observation.members);
}

bool AudioMixerRoster::replace(std::vector<AudioMixerMember> members)
{
  std::unordered_map<std::string, AudioMixerMember> replacement;
  std::unordered_map<std::string, std::string> uuid_by_name;
  replacement.reserve(members.size());

  for (auto& member : members)
  {
    if (member.input_uuid.empty() || member.input_name.empty())
      continue;
    uuid_by_name[member.input_name] = member.input_uuid;
    replacement.insert_or_assign(member.input_uuid, std::move(member));
  }

  std::lock_guard lock(mutex_);
  const bool changed = !observed_ || replacement != inputs_by_uuid_;
  if (changed)
  {
    for (const auto& [uuid, member] : replacement)
    {
      const auto previous = inputs_by_uuid_.find(uuid);
      if (previous == inputs_by_uuid_.end())
        recent_changes_.push_back(
            {{"inputUuid", uuid}, {"change", "added"}, {"reason", member.membership_reasons}});
      else if (previous->second != member)
        recent_changes_.push_back(
            {{"inputUuid", uuid}, {"change", "metadata"}, {"reason", member.membership_reasons}});
    }
    for (const auto& [uuid, _] : inputs_by_uuid_)
      if (!replacement.contains(uuid))
        recent_changes_.push_back(
            {{"inputUuid", uuid}, {"change", "removed"}, {"reason", "not_in_obs_mixer"}});
    if (recent_changes_.size() > 32)
      recent_changes_.erase(recent_changes_.begin(), recent_changes_.end() - 32);
  }

  observed_ = true;
  inputs_by_uuid_ = std::move(replacement);
  input_uuid_by_name_ = std::move(uuid_by_name);
  observation_available_.notify_all();
  return changed;
}

bool AudioMixerRoster::observe_telemetry(const std::vector<std::string>& input_names)
{
  std::unordered_set<std::string> names;
  std::unordered_set<std::string> uuids;
  for (const auto& name : input_names)
    if (!name.empty())
      names.insert(name);

  std::lock_guard lock(mutex_);
  for (const auto& name : names)
  {
    const auto found = input_uuid_by_name_.find(name);
    if (found != input_uuid_by_name_.end())
      uuids.insert(found->second);
  }
  const bool unknown_changed =
      names != telemetry_names_ &&
      std::any_of(names.begin(),
                  names.end(),
                  [&](const auto& name) { return !input_uuid_by_name_.contains(name); });
  telemetry_names_ = std::move(names);
  telemetry_uuids_ = std::move(uuids);
  return unknown_changed;
}

AudioMixerRosterSnapshot
AudioMixerRoster::wait_for_first_observation(std::chrono::milliseconds timeout) const
{
  std::unique_lock lock(mutex_);
  observation_available_.wait_for(lock, timeout, [this] { return observed_; });
  return {observed_, inputs_by_uuid_};
}

json AudioMixerRoster::diagnostics() const
{
  std::lock_guard lock(mutex_);
  std::unordered_set<std::string> canonical;
  for (const auto& [uuid, _] : inputs_by_uuid_)
    canonical.insert(uuid);
  return {{"observed", observed_},
          {"sourceSequence", source_sequence_},
          {"canonicalRosterUuids", sorted_values(canonical)},
          {"telemetryUuids", sorted_values(telemetry_uuids_)},
          {"telemetryNames", sorted_values(telemetry_names_)},
          {"recentMembershipChanges", recent_changes_}};
}

} // namespace studio_remote
