#include <studio_remote/teleprompter/memory_session_repository.hpp>

#include "memory_session_validation.hpp"

#include <stdexcept>
#include <utility>

namespace studio_remote::teleprompter
{
using namespace runtime;

MemorySessionRepository::MemorySessionRepository(SessionDeviceSource devices,
                                                 ScriptCatalogSource scripts,
                                                 std::function<std::string()> new_id,
                                                 std::function<std::uint64_t()> monotonic_ms)
    : devices_(std::move(devices)), scripts_(std::move(scripts)), new_id_(std::move(new_id)),
      monotonic_ms_(std::move(monotonic_ms))
{
  if (!devices_ || !scripts_ || !new_id_ || !monotonic_ms_)
    throw std::invalid_argument("Shared teleprompter requires resources, ID and clock providers");
  clock_id_ = new_id_();
  if (clock_id_.empty() || clock_id_.size() > 128)
    throw std::invalid_argument("Invalid shared teleprompter clock ID");
}

MemorySessionRepository::Resources MemorySessionRepository::resources()
{
  for (;;)
  {
    auto previous = next_observation_.load();
    auto scripts = scripts_();
    auto devices = devices_();
    const auto now = monotonic_ms_();
    // An overlapping sample may finish first. Retry its older inputs instead of assigning them
    // a newer observation and letting them invalidate a newly authored script or paired device.
    if (next_observation_.compare_exchange_strong(previous, previous + 1))
      return {previous + 1, std::move(scripts), std::move(devices), now};
  }
}

bool MemorySessionRepository::reconcile(Record& record, const Resources& resources)
{
  // An older concurrent request must not roll back a newer revision or invalidate a new pairing.
  if (resources.observation < record.observation)
    return true;
  record.observation = resources.observation;
  auto& speed = record.state.speed_adjustment;
  if (speed.expires_at_monotonic_ms != 0 && resources.now >= speed.expires_at_monotonic_ms)
  {
    speed.displacement = 0;
    speed.expires_at_monotonic_ms = 0;
  }
  for (auto member = record.members.begin(); member != record.members.end();)
  {
    const auto device = resources.devices.find(member->device_id);
    const bool current =
        device != resources.devices.end() && device->second.provider == record.state.provider &&
        device->second.pairing_generation == record.generations.at(member->device_id).first;
    if (current && allowed(device->second, member->role) &&
        device->second.session_authorization_generation ==
            record.generations.at(member->device_id).second)
    {
      ++member;
      continue;
    }
    if (member->device_id == record.state.owner_device_id || member->role != SessionRole::monitor ||
        (current && record.state.provider == "companion-teleprompter"))
      return false;
    if (record.state.version >= maximum_safe_integer)
      return false;
    record.generations.erase(member->device_id);
    member = record.members.erase(member);
    ++record.state.version;
    record.state.alignment.reset();
  }
  for (auto& [id, loaded] : record.scripts)
  {
    const auto found = resources.scripts->find(id);
    if (found == resources.scripts->end() ||
        !found->second->assigned_to(resources.devices.at(record.state.owner_device_id)) ||
        found->second->assignments.at(record.state.owner_device_id) !=
            loaded->assignments.at(record.state.owner_device_id))
      return false;
    // Immutable revisions can only advance within a live library.
    if (found->second->script.summary.revision <= loaded->script.summary.revision)
      continue;
    loaded = found->second;
    if (id != record.state.script_id)
      continue;
    if (record.state.version >= maximum_safe_integer)
      return false;
    auto& state = record.state;
    ++state.version;
    state.script_revision = loaded->script.summary.revision;
    state.playback = PlaybackState::ready;
    state.elapsed_ms = 0;
    state.word_position = 0;
    state.anchor_monotonic_ms = 0;
    state.seek_version = state.version;
    state.seek_byte_offset = 0;
    state.seek_line_offset = 0;
    state.alignment.reset();
    state.voice_follow = {};
    state.timed_pause.reset();
    state.force_continue = false;
    state.startup_bypass = false;
    state.speed_adjustment = {state.speed_adjustment.epoch + 1, 0, 0, 0};
    if (state.settings.voice_follow_enabled)
      state.voice_follow.phase = VoiceFollowPhase::paused;
  }
  return true;
}

MemorySessionRepository::Record* MemorySessionRepository::find(std::string_view id,
                                                               const Resources& resources)
{
  const auto found = records_.find(id);
  if (found == records_.end())
    return nullptr;
  if (!reconcile(found->second, resources))
  {
    records_.erase(found);
    return nullptr;
  }
  return &found->second;
}

std::optional<SessionRole> MemorySessionRepository::role(const Record& record,
                                                         std::string_view device)
{
  for (const auto& member : record.members)
    if (member.device_id == device)
      return member.role;
  return std::nullopt;
}

SessionSnapshot
MemorySessionRepository::snapshot(const Record& record, SessionRole role, std::uint64_t now) const
{
  const auto& loaded = *record.scripts.at(record.state.script_id);
  const auto& words = loaded.word_offsets;
  const auto position = record.state.word_position >= static_cast<double>(words.size())
                            ? loaded.script.document.spoken_text.size()
                            : words[static_cast<std::size_t>(record.state.word_position)];
  std::vector<Summary> scripts;
  scripts.reserve(record.scripts.size());
  for (const auto& [id, script] : record.scripts)
    scripts.push_back(script->script.summary);
  std::sort(scripts.begin(),
            scripts.end(),
            [](const Summary& left, const Summary& right)
            {
              if (left.modified_at_unix_ms != right.modified_at_unix_ms)
                return left.modified_at_unix_ms > right.modified_at_unix_ms;
              return left.id < right.id;
            });
  return {record.state, clock_id_, now, position, role, std::move(scripts), record.members};
}

Result<SessionSnapshot> MemorySessionRepository::assign(std::string_view owner,
                                                        const SessionAssignment& assignment)
{
  const auto resources = this->resources();
  const auto id = new_id_();
  if (!valid_assignment(resources.devices, *resources.scripts, owner, assignment))
    return session_failure<SessionSnapshot>(Error::invalid_script);
  Record record;
  record.observation = resources.observation;
  record.state.id = id;
  record.state.owner_device_id = owner;
  record.state.provider = resources.devices.find(owner)->second.provider;
  record.members = assignment.members;
  record.members.push_back({std::string(owner), assignment.owner_role});
  std::sort(record.members.begin(),
            record.members.end(),
            [](const auto& left, const auto& right) { return left.device_id < right.device_id; });
  for (const auto& member : record.members)
    record.generations.emplace(
        member.device_id,
        std::pair{resources.devices.at(member.device_id).pairing_generation,
                  resources.devices.at(member.device_id).session_authorization_generation});
  for (const auto& script : assignment.script_ids)
    record.scripts.emplace(script, resources.scripts->at(script));
  const auto& script = record.scripts.at(assignment.script_ids.front())->script;
  record.state.script_id = script.summary.id;
  record.state.script_revision = script.summary.revision;
  record.state.settings.words_per_minute = script.document.draft.words_per_minute;
  std::lock_guard lock(mutex_);
  std::erase_if(records_, [&](auto& entry) { return !reconcile(entry.second, resources); });
  const auto count = std::count_if(records_.begin(),
                                   records_.end(),
                                   [owner](const auto& entry)
                                   { return entry.second.state.owner_device_id != owner; });
  if (count >= 64)
    return session_failure<SessionSnapshot>(Error::limit_reached);
  // Allocate the replacement before removing the owner's previous assignment.
  const auto [inserted, fresh] = records_.emplace(id, std::move(record));
  if (!fresh)
    return session_failure<SessionSnapshot>(Error::conflict);
  std::erase_if(records_,
                [owner, &id](const auto& entry)
                { return entry.first != id && entry.second.state.owner_device_id == owner; });
  return {snapshot(inserted->second, assignment.owner_role, resources.now), Error::none, {}};
}

Result<bool> MemorySessionRepository::unassign(std::string_view owner,
                                               std::string_view id,
                                               std::uint64_t expected_version)
{
  const auto resources = this->resources();
  std::lock_guard lock(mutex_);
  const auto* record = find(id, resources);
  if (!record || record->state.owner_device_id != owner)
    return session_failure<bool>(Error::not_found);
  if (record->state.version != expected_version)
    return session_failure<bool>(Error::conflict);
  records_.erase(std::string(id));
  return {true, Error::none, {}};
}

Result<std::vector<SessionSnapshot>> MemorySessionRepository::sessions(std::string_view device)
{
  const auto resources = this->resources();
  const auto found = resources.devices.find(device);
  if (found == resources.devices.end() || found->second.allowed_roles.empty())
    return session_failure<std::vector<SessionSnapshot>>(Error::not_found);
  std::lock_guard lock(mutex_);
  std::erase_if(records_, [&](auto& entry) { return !reconcile(entry.second, resources); });
  std::vector<SessionSnapshot> result;
  for (const auto& [id, record] : records_)
    if (const auto member = role(record, device))
      result.push_back(snapshot(record, *member, resources.now));
  return {std::move(result), Error::none, {}};
}

Result<SessionSnapshot> MemorySessionRepository::session(std::string_view device,
                                                         std::string_view id)
{
  const auto resources = this->resources();
  std::lock_guard lock(mutex_);
  const auto* record = find(id, resources);
  const auto member = record ? role(*record, device) : std::nullopt;
  if (!member)
    return session_failure<SessionSnapshot>(Error::not_found);
  return {snapshot(*record, *member, resources.now), Error::none, {}};
}

Result<Script> MemorySessionRepository::session_script(std::string_view device,
                                                       std::string_view id,
                                                       std::string_view script_id)
{
  const auto resources = this->resources();
  std::shared_ptr<const LoadedScript> loaded;
  {
    std::lock_guard lock(mutex_);
    const auto* record = find(id, resources);
    if (!record || !role(*record, device))
      return session_failure<Script>(Error::not_found);
    const auto found = record->scripts.find(script_id);
    if (found == record->scripts.end())
      return session_failure<Script>(Error::not_found);
    loaded = found->second;
  }
  // Copy potentially large content after releasing the session mutex.
  return {loaded->script, Error::none, {}};
}

Result<SessionState> MemorySessionRepository::change_state(const Record& record,
                                                           const SessionAction& action,
                                                           std::uint64_t now)
{
  auto state = record.state;
  if (const auto* selected = std::get_if<SelectSessionScript>(&action))
  {
    const auto script = record.scripts.find(selected->script_id);
    if (script == record.scripts.end())
      return session_failure<SessionState>(Error::not_found);
    if (script->second->script.summary.revision != selected->revision)
      return session_failure<SessionState>(Error::conflict);
    state.script_id = selected->script_id;
    state.script_revision = selected->revision;
    state.elapsed_ms = 0;
    state.word_position = 0;
    state.seek_version = state.version + 1;
    state.seek_byte_offset = 0;
    state.seek_line_offset = 0;
    state.timed_pause.reset();
    state.force_continue = false;
    state.startup_bypass = false;
    state.speed_adjustment = {state.speed_adjustment.epoch + 1, 0, 0, 0};
    state.anchor_monotonic_ms = now;
    state.playback = PlaybackState::ready;
    return {std::move(state), Error::none, {}};
  }
  return apply_session_action(
      std::move(state), record.scripts.at(record.state.script_id)->script.document, action, now);
}

Result<SessionCommandResult> MemorySessionRepository::command(std::string_view device,
                                                              std::string_view id,
                                                              const SessionCommand& command)
{
  const auto resources = this->resources();
  const auto identity = session_command_identity(command);
  std::lock_guard lock(mutex_);
  auto* record = find(id, resources);
  const auto member = record ? role(*record, device) : std::nullopt;
  if (!member)
    return session_failure<SessionCommandResult>(Error::not_found);
  if (*member != SessionRole::controller)
    return session_failure<SessionCommandResult>(Error::control_denied);
  std::erase_if(record->commands,
                [&resources](const auto& previous)
                {
                  return resources.now >= previous.received_ms &&
                         resources.now - previous.received_ms >= 3'600'000;
                });
  for (const auto& previous : record->commands)
  {
    if (previous.device_id != device || previous.command_id != command.command_id)
      continue;
    if (previous.identity != identity)
      return session_failure<SessionCommandResult>(Error::conflict);
    return {SessionCommandResult{
                snapshot(*record, *member, resources.now), previous.accepted_version, true},
            Error::none,
            {}};
  }
  if (command.expected_version != record->state.version ||
      record->state.version >= maximum_safe_integer)
    return session_failure<SessionCommandResult>(Error::conflict);
  auto changed = change_state(*record, command.action, resources.now);
  if (!changed)
    return {{}, changed.error, changed.message};
  ++changed.value->version;
  // Transient operator controls do not invalidate Master geometry or recognition.
  if (!std::holds_alternative<AdjustSessionSpeed>(command.action) &&
      !std::holds_alternative<ForceContinueSession>(command.action) &&
      !std::holds_alternative<SetVideoMonitor>(command.action))
  {
    changed.value->alignment.reset();
    changed.value->voice_follow = {};
    if (changed.value->settings.voice_follow_enabled)
      changed.value->voice_follow.phase = changed.value->playback == PlaybackState::playing
                                              ? VoiceFollowPhase::acquiring
                                              : VoiceFollowPhase::paused;
  }
  record->commands.push_back(
      {std::string(device), command.command_id, identity, changed.value->version, resources.now});
  if (record->commands.size() > 256)
    record->commands.pop_front();
  record->state = std::move(*changed.value);
  return {
      SessionCommandResult{snapshot(*record, *member, resources.now), record->state.version, false},
      Error::none,
      {}};
}

Result<SessionSnapshot> MemorySessionRepository::publish_position(
    std::string_view device, std::string_view id, const SessionPositionReport& report)
{
  const auto resources = this->resources();
  const auto now = resources.now;
  std::lock_guard lock(mutex_);
  auto* record = find(id, resources);
  const auto member = record ? role(*record, device) : std::nullopt;
  if (!member)
    return session_failure<SessionSnapshot>(Error::not_found);
  if (*member != SessionRole::master)
    return session_failure<SessionSnapshot>(Error::control_denied);
  auto state = record->state;
  if (report.clock_id != clock_id_ || report.expected_version != state.version ||
      report.sequence <= state.position_sequence ||
      report.applied_seek_version < state.applied_seek_version ||
      report.applied_seek_version > state.seek_version)
    return session_failure<SessionSnapshot>(Error::conflict);
  const auto& loaded = *record->scripts.at(state.script_id);
  const auto words = loaded.word_offsets.size();
  const auto captured = report.sampled_at_monotonic_ms;
  const bool invalid_time = captured > maximum_safe_integer ||
                            (captured > now && captured - now > 5000) ||
                            (captured < now && now - captured > 30000);
  const bool completing = state.playback == PlaybackState::playing &&
                          report.playback == PlaybackState::completed &&
                          report.word_position == static_cast<double>(words);
  if (invalid_time ||
      !valid_voice_follow_state(report.voice_follow, state.settings.voice_follow_enabled) ||
      (report.timed_pause &&
       !valid_timed_pause(*report.timed_pause, loaded.script.document.spoken_text)) ||
      !std::isfinite(report.master_width_capacity) || report.master_width_capacity <= 0.0 ||
      (report.alignment &&
       !valid_alignment(*report.alignment, loaded.script.document.spoken_text)) ||
      report.sequence > maximum_safe_integer || !std::isfinite(report.word_position) ||
      report.word_position < 0.0 || report.word_position > static_cast<double>(words) ||
      report.elapsed_ms > 86'400'000 || (report.playback != state.playback && !completing))
    return {{}, Error::invalid_progress, "Invalid master teleprompter position"};
  state.voice_follow = report.voice_follow;
  state.timed_pause = report.timed_pause;
  state.word_position = report.word_position;
  state.applied_seek_version = report.applied_seek_version;
  state.elapsed_ms = report.elapsed_ms;
  state.position_sequence = report.sequence;
  state.anchor_monotonic_ms = captured;
  state.master_width_capacity = report.master_width_capacity;
  if (constrain_master_width(state))
  {
    if (state.version >= maximum_safe_integer)
      return session_failure<SessionSnapshot>(Error::conflict);
    ++state.version;
    state.alignment.reset();
  }
  else if (report.alignment)
  {
    state.alignment = report.alignment;
    state.alignment->sampled_at_monotonic_ms = captured;
    state.alignment->version = state.version;
  }
  if (completing)
  {
    state.playback = PlaybackState::completed;
    state.speed_adjustment = {state.speed_adjustment.epoch + 1, 0, 0, 0};
  }
  record->state = std::move(state);
  return {snapshot(*record, *member, now), Error::none, {}};
}
} // namespace studio_remote::teleprompter
