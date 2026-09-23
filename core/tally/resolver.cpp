#include <studio_remote/tally/resolver.hpp>

#include <algorithm>
#include <array>

namespace studio_remote::tally
{
namespace
{
bool matches(const Selection& selection, const std::optional<Selection>& current)
{
  return current && !selection.id.empty() && selection.id == current->id;
}

bool contains(const std::vector<Selection>& selections, const std::optional<Selection>& current)
{
  return std::ranges::any_of(selections,
                             [&](const auto& value) { return matches(value, current); });
}

std::vector<Selection> source_scenes(const Definition& definition, const Signals& signals)
{
  std::set<std::string> ids;
  for (const auto& source : signals.source_catalog)
  {
    if (contains(definition.sources, source.source))
    {
      ids.insert(source.active_scene_ids.begin(), source.active_scene_ids.end());
    }
  }
  std::vector<Selection> scenes;
  for (const auto& scene : signals.available_scenes)
  {
    if (ids.contains(scene.id))
    {
      scenes.push_back(scene);
    }
  }
  return scenes;
}

using CountdownCandidates = std::array<std::optional<Countdown>, 2>;

template <typename Predicate>
std::optional<Countdown> first_countdown(const CountdownCandidates& candidates, Predicate predicate)
{
  for (const auto& countdown : candidates)
  {
    if (countdown && predicate(*countdown))
    {
      return countdown;
    }
  }
  return std::nullopt;
}

State output_state(const Signals& signals, State otherwise)
{
  if (signals.recording && signals.streaming)
  {
    return State::live;
  }
  if (signals.streaming)
  {
    return State::streaming;
  }
  return signals.recording ? State::recording : otherwise;
}

Resolution scene_flood(const Definition& definition,
                       const Signals& signals,
                       const CountdownCandidates& candidates,
                       bool program,
                       bool preview)
{
  Resolution result;
  result.preview_also = program && preview;
  result.countdown =
      first_countdown(candidates,
                      [&](const Countdown& countdown)
                      {
                        return countdown.operation == CountdownOperation::start_outputs
                                   ? program
                                   : preview && contains(definition.scenes, countdown.target_scene);
                      });
  if (result.countdown)
  {
    result.state = result.countdown->operation == CountdownOperation::start_outputs ? State::silence
                                                                                    : State::next;
  }
  else
  {
    result.state = program   ? output_state(signals, State::program)
                   : preview ? State::preview
                             : State::off;
  }
  return result;
}

Resolution scene_dot(const Definition& definition,
                     const Signals& signals,
                     const CountdownCandidates& candidates,
                     bool program)
{
  Resolution result;
  const bool output_active = signals.recording || signals.streaming;
  result.countdown = first_countdown(
      candidates,
      [&](const Countdown& countdown)
      {
        return countdown.operation == CountdownOperation::start_outputs
                   ? program
                   : output_active && contains(definition.scenes, countdown.target_scene);
      });
  result.state = result.countdown           ? State::countdown
                 : program && output_active ? State::program
                                            : State::off;
  return result;
}

bool relevant(const Definition& definition,
              const Signals& signals,
              const Countdown& countdown,
              bool program,
              bool preview)
{
  if (countdown.operation == CountdownOperation::start_outputs)
  {
    if ((definition.mode == Mode::bar || definition.mode == Mode::operator_view) &&
        definition.binding == Binding::scene)
    {
      return program;
    }
    return definition.binding != Binding::scene || program || preview;
  }
  return definition.binding == Binding::all ||
         (definition.mode == Mode::dot && definition.binding == Binding::door &&
          (signals.recording || signals.streaming)) ||
         (definition.binding == Binding::scene &&
          contains(definition.scenes, countdown.target_scene));
}

State confirmed_state(const Definition& definition,
                      const Signals& signals,
                      bool program,
                      bool preview)
{
  if (definition.binding == Binding::door)
  {
    return definition.standby ? State::standby : output_state(signals, State::off);
  }
  if (program || (definition.binding == Binding::all && signals.program))
  {
    return definition.mode == Mode::bar || definition.mode == Mode::flood
               ? output_state(signals, State::program)
               : State::program;
  }
  if (preview)
  {
    return State::preview;
  }
  return definition.standby ? State::standby : State::off;
}

Resolution resolve_available(const Definition& definition,
                             const Signals& signals,
                             const CountdownCandidates& candidates)
{
  const bool program = contains(definition.scenes, signals.program);
  const bool preview = signals.preview_available && contains(definition.scenes, signals.preview);
  if (definition.binding == Binding::scene && definition.mode == Mode::flood)
  {
    return scene_flood(definition, signals, candidates, program, preview);
  }
  if (definition.binding == Binding::scene && definition.mode == Mode::dot)
  {
    return scene_dot(definition, signals, candidates, program);
  }
  Resolution result;
  result.preview_also = program && preview;
  result.countdown =
      first_countdown(candidates,
                      [&](const Countdown& countdown)
                      { return relevant(definition, signals, countdown, program, preview); });
  result.next_countdown =
      first_countdown(candidates,
                      [&](const Countdown& countdown)
                      {
                        return countdown.operation == CountdownOperation::studio_transition &&
                               contains(signals.available_scenes, countdown.target_scene);
                      });
  result.state =
      result.countdown ? State::countdown : confirmed_state(definition, signals, program, preview);
  return result;
}

bool resolve_source_binding(const Definition& definition,
                            const Signals& signals,
                            Definition& effective,
                            Resolution& result)
{
  if (!signals.source_catalog_ready)
  {
    result.state = State::source_data_unavailable;
    return false;
  }
  for (const auto& selected : definition.sources)
  {
    if (std::ranges::none_of(signals.source_catalog,
                             [&](const auto& source) { return source.source.id == selected.id; }))
    {
      result.unavailable_sources.push_back(selected);
    }
  }
  if (result.unavailable_sources.size() == definition.sources.size())
  {
    result.state = State::source_unavailable;
    return false;
  }
  effective.binding = Binding::scene;
  effective.scenes = source_scenes(definition, signals);
  return true;
}

bool resolve_scene_binding(const Definition& definition,
                           const Signals& signals,
                           Definition& effective,
                           Resolution& result)
{
  effective.scenes.clear();
  for (const auto& selected : definition.scenes)
  {
    const auto current = std::ranges::find(signals.available_scenes, selected.id, &Selection::id);
    if (current == signals.available_scenes.end())
    {
      result.unavailable_scenes.push_back(selected);
    }
    else
    {
      effective.scenes.push_back(*current);
    }
  }
  if (effective.scenes.empty())
  {
    result.state = State::scene_unavailable;
    return false;
  }
  return true;
}
} // namespace

Resolution resolve(const Definition& definition,
                   const Signals& signals,
                   const std::optional<Countdown>& author_countdown,
                   const std::optional<Countdown>& other_countdown)
{
  Resolution result;
  if (!signals.connected)
  {
    return result;
  }
  auto effective = definition;
  if (definition.binding == Binding::source &&
      !resolve_source_binding(definition, signals, effective, result))
  {
    return result;
  }
  if (definition.binding == Binding::scene &&
      !resolve_scene_binding(definition, signals, effective, result))
  {
    return result;
  }
  const CountdownCandidates candidates =
      definition.advanced_countdowns ? CountdownCandidates{author_countdown, other_countdown}
                                     : CountdownCandidates{};
  auto available = resolve_available(effective, signals, candidates);
  available.unavailable_scenes = std::move(result.unavailable_scenes);
  available.unavailable_sources = std::move(result.unavailable_sources);
  return available;
}

std::optional<SourceScene>
source_scene(const Definition& definition, const Signals& signals, const Resolution& resolution)
{
  if (definition.binding != Binding::source || !signals.source_catalog_ready ||
      resolution.state == State::off || resolution.state == State::standby ||
      resolution.state == State::disconnected || resolution.state == State::source_unavailable ||
      resolution.state == State::source_data_unavailable)
  {
    return std::nullopt;
  }
  const auto scenes = source_scenes(definition, signals);
  std::optional<Selection> selected;
  if (resolution.countdown &&
      resolution.countdown->operation == CountdownOperation::studio_transition)
  {
    selected = resolution.countdown->target_scene;
  }
  else
  {
    selected = contains(scenes, signals.program) ? signals.program
               : signals.preview_available       ? signals.preview
                                                 : std::nullopt;
  }
  if (!contains(scenes, selected))
  {
    return std::nullopt;
  }
  const auto current = std::ranges::find(scenes, selected->id, &Selection::id);
  SourceScene result{*current, {}};
  for (const auto& source : signals.source_catalog)
  {
    if (contains(definition.sources, source.source) &&
        source.active_scene_ids.contains(current->id))
    {
      result.sources.push_back(source.source);
    }
  }
  return result;
}
} // namespace studio_remote::tally
