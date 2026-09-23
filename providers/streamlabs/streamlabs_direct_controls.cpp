#include <studio_remote/streamlabs/streamlabs_adapter.hpp>

#include <algorithm>
#include <chrono>
#include <studio_remote/streamlabs/streamlabs_direct_catalogs.hpp>

namespace studio_remote::streamlabs
{
namespace
{
bool matches_binding(const ConfiguredAction& action, std::span<const HotkeyKey> requested)
{
  if (requested.empty())
    return false;
  return std::ranges::any_of(action.bindings,
                             [&](const auto& binding)
                             {
                               std::vector<HotkeyKey> keys;
                               for (const auto& name : binding)
                               {
                                 const auto key = parse_hotkey_key(name);
                                 if (!key)
                                   return false;
                                 keys.push_back(*key);
                               }
                               return std::ranges::is_permutation(keys, requested);
                             });
}
} // namespace

std::optional<ProductionCanvasState> StreamlabsAdapter::canvas_state() const
{
  std::lock_guard lock(mutex_);
  return state_.canvas;
}
std::optional<ProductionSceneCollectionCatalog> StreamlabsAdapter::scene_collection_catalog()
{
  std::lock_guard lock(mutex_);
  if (!state_.collections)
    return std::nullopt;
  ProductionSceneCollectionCatalog result{collection_revision_, state_.active_collection.id, {}};
  for (const auto& choice : *state_.collections)
    result.choices.push_back({choice.id, choice.name});
  return result;
}
bool StreamlabsAdapter::select_scene_collection(std::string_view id,
                                                std::uint64_t expected_revision)
{
  std::lock_guard command(command_mutex_);
  refresh();
  const auto catalog = scene_collection_catalog();
  if (!catalog)
    throw ProductionConfigurationError("production-backend-unavailable");
  if (catalog->revision != expected_revision)
    throw ProductionConfigurationError("collection-changed");
  if (!std::ranges::any_of(catalog->choices, [&](const auto& c) { return c.id == id; }))
    throw ProductionConfigurationError("collection-not-found");
  {
    std::lock_guard transport(transport_mutex_);
    const auto [recording, streaming] = transport_->read_output_states();
    if (recording != OutputState::offline || streaming != OutputState::offline)
      throw ProductionConfigurationError("outputs-active");
    if (catalog->selected_id == id)
      return false;
    transport_->select_collection(id);
  }
  refresh();
  std::lock_guard lock(mutex_);
  if (state_.active_collection.id != id)
    throw ProductionConfigurationError("collection-load-failed");
  return true;
}
bool StreamlabsAdapter::hotkey_catalog_supported() const noexcept
{
  std::lock_guard lock(mutex_);
  return state_.configured_actions.has_value();
}
std::optional<std::vector<ProductionHotkeyDefinition>> StreamlabsAdapter::hotkeys()
{
  std::optional<std::vector<ConfiguredAction>> actions;
  {
    std::lock_guard transport(transport_mutex_);
    actions = transport_->read_configured_actions();
  }
  {
    std::lock_guard lock(mutex_);
    state_.configured_actions = actions;
  }
  if (!actions)
    return std::nullopt;
  std::vector<ProductionHotkeyDefinition> result;
  for (const auto& action : *actions)
    result.push_back({action.id, action.label, action.bindings.front(), action.bindings});
  return result;
}
void StreamlabsAdapter::trigger_hotkey(const ProductionHotkeyTrigger& trigger)
{
  std::lock_guard command(command_mutex_);
  if (trigger.id.empty())
    throw HotkeyExecutionError("production-hotkey-unsupported",
                               "Only assigned Streamlabs actions can be triggered");
  std::string expected_program;
  {
    std::lock_guard transport(transport_mutex_);
    const auto actions = transport_->read_configured_actions();
    if (!actions || !std::ranges::any_of(*actions,
                                         [&](const auto& action)
                                         {
                                           return action.id == trigger.id &&
                                                  matches_binding(action, trigger.keys);
                                         }))
      throw HotkeyExecutionError("production-hotkey-not-found",
                                 "Streamlabs configured hotkey binding is no longer assigned");
    const auto before = transport_->read_state();
    if (trigger.id == "TRANSITION_STUDIO_MODE")
    {
      if (!before.studio_mode_enabled || !before.program_identity_available)
        throw HotkeyExecutionError("production-hotkey-unavailable",
                                   "Streamlabs Studio transition is unavailable");
      expected_program = before.preview_scene_id;
      transport_->execute_studio_transition();
    }
    else if (trigger.id == "ENABLE_STUDIO_MODE" || trigger.id == "DISABLE_STUDIO_MODE")
    {
      const bool desired = trigger.id == "ENABLE_STUDIO_MODE";
      if (before.studio_mode_enabled != desired)
        transport_->set_studio_mode(desired);
    }
    else if (trigger.id == "TOGGLE_START_RECORDING" || trigger.id == "TOGGLE_STOP_RECORDING")
    {
      if (output_state_active(before.recording) != (trigger.id == "TOGGLE_START_RECORDING"))
        transport_->toggle_recording();
    }
    else if (trigger.id == "TOGGLE_START_STREAMING" || trigger.id == "TOGGLE_STOP_STREAMING")
    {
      if (output_state_active(before.streaming) != (trigger.id == "TOGGLE_START_STREAMING"))
        transport_->toggle_streaming();
    }
  }
  // Android executeConfiguredHotKey/converge: dispatch once, then observe the desired state.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do
  {
    refresh();
    std::unique_lock lock(mutex_);
    const bool desired = trigger.id == "ENABLE_STUDIO_MODE" ||
                         trigger.id == "TOGGLE_START_RECORDING" ||
                         trigger.id == "TOGGLE_START_STREAMING";
    const bool reached =
        trigger.id == "TRANSITION_STUDIO_MODE" ? state_.program_scene_id == expected_program
        : trigger.id.ends_with("RECORDING")    ? output_state_active(state_.recording) == desired
        : trigger.id.ends_with("STREAMING")    ? output_state_active(state_.streaming) == desired
                                               : state_.studio_mode_enabled == desired;
    if (reached)
      return;
    wake_.wait_for(lock, std::chrono::milliseconds(100), [this] { return stopping_.load(); });
  } while (!stopping_ && std::chrono::steady_clock::now() < deadline);
  throw HotkeyExecutionError("production-hotkey-result-uncertain",
                             "Streamlabs Desktop did not confirm the requested state change");
}
} // namespace studio_remote::streamlabs
