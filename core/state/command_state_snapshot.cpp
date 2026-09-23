#include <studio_remote/state/command_state_snapshot.hpp>

#include <studio_remote/state/state_store.hpp>

#include <algorithm>

namespace studio_remote
{

std::uint64_t CommandStateSnapshot::revision() const noexcept
{
  return revision_;
}

bool CommandStateSnapshot::fresh() const noexcept
{
  return fresh_;
}

bool CommandStateSnapshot::studio_mode() const noexcept
{
  return studio_mode_;
}

std::uint32_t CommandStateSnapshot::transition_duration_ms() const noexcept
{
  return transition_duration_ms_;
}

std::string_view CommandStateSnapshot::program_scene_id() const noexcept
{
  return program_scene_id_;
}

std::string_view CommandStateSnapshot::preview_scene_id() const noexcept
{
  return preview_scene_id_;
}

std::uint64_t CommandStateSnapshot::program_frame_ns() const noexcept
{
  return program_frame_ns_;
}

bool CommandStateSnapshot::transition_completion_requires_video() const noexcept
{
  return transition_completion_requires_video_;
}

bool CommandStateSnapshot::stream_active() const noexcept
{
  return stream_active_;
}

bool CommandStateSnapshot::stream_configured() const noexcept
{
  return stream_configured_;
}

std::optional<StreamingDestination>
CommandStateSnapshot::streaming_destination(std::string_view id) const
{
  const auto found = streaming_destinations_.find(std::string(id));
  return found == streaming_destinations_.end() ? std::nullopt : std::optional(found->second);
}

std::vector<StreamingDestination> CommandStateSnapshot::streaming_destinations() const
{
  std::vector<StreamingDestination> values;
  values.reserve(streaming_destinations_.size());
  for (const auto& [id, destination] : streaming_destinations_)
  {
    static_cast<void>(id);
    values.push_back(destination);
  }
  std::sort(values.begin(),
            values.end(),
            [](const StreamingDestination& left, const StreamingDestination& right)
            { return left.order != right.order ? left.order < right.order : left.id < right.id; });
  return values;
}

bool CommandStateSnapshot::record_active() const noexcept
{
  return record_active_;
}

bool CommandStateSnapshot::record_paused() const noexcept
{
  return record_paused_;
}

std::optional<Scene> CommandStateSnapshot::scene(std::string_view id) const
{
  const auto found = scenes_.find(std::string(id));
  return found == scenes_.end() ? std::nullopt : std::optional<Scene>(found->second);
}

std::optional<SceneItem> CommandStateSnapshot::item(std::string_view scene_id,
                                                    std::int64_t item_id) const
{
  const auto roots = items_.find(std::string(scene_id));
  if (roots != items_.end())
  {
    const auto found = std::find_if(roots->second.begin(),
                                    roots->second.end(),
                                    [item_id](const auto& item) { return item.id == item_id; });
    if (found != roots->second.end())
      return *found;
  }

  const auto prefix = std::string(scene_id) + ":";
  for (const auto& [key, children] : group_items_)
  {
    if (!key.starts_with(prefix))
      continue;
    const auto found = std::find_if(children.begin(),
                                    children.end(),
                                    [item_id](const auto& item) { return item.id == item_id; });
    if (found != children.end())
      return *found;
  }
  return std::nullopt;
}

std::optional<AudioInput> CommandStateSnapshot::input(std::string_view id) const
{
  const auto found = inputs_.find(std::string(id));
  return found == inputs_.end() ? std::nullopt : std::optional<AudioInput>(found->second);
}

std::optional<AudioMaster> CommandStateSnapshot::audio_master() const
{
  return audio_master_;
}

std::optional<SceneTransition> CommandStateSnapshot::transition(std::string_view id) const
{
  const auto found = transitions_.find(std::string(id));
  return found == transitions_.end() ? std::nullopt : std::optional<SceneTransition>(found->second);
}

std::size_t CommandStateSnapshot::solo_input_count() const noexcept
{
  return static_cast<std::size_t>(std::count_if(
      inputs_.begin(), inputs_.end(), [](const auto& entry) { return entry.second.solo; }));
}

} // namespace studio_remote
