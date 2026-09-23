#include "gateway.hpp"

#include <chrono>
#include <studio_remote/obs/obs_event_reader.hpp>
#include <utility>

namespace studio_remote
{

GatewayRuntime::GatewayRuntime(GatewayIdentity identity,
                               IObsAdapter& obs,
                               AuditStore& audit,
                               IThumbnailCapture* thumbnail_capture,
                               IThumbnailEncoder* thumbnail_encoder,
                               IAudioMixerRosterSource* audio_mixer_source,
                               ISceneItemPropertiesSource* scene_item_properties_source,
                               ISourceMediaInspector* source_media_inspector)
    : obs_(obs), obs_request_arbiter_(obs),
      ordinary_obs_(obs, obs_request_arbiter_, ObsRequestLane::ordinary), audit_(audit),
      state_(identity.instance_id, identity.name), audio_mixer_roster_(audio_mixer_source),
      scene_item_properties_(scene_item_properties_source),
      obs_configuration_(ordinary_obs_, audit, [this] { request_reconcile(); }),
      reconciler_(
          ordinary_obs_, audio_mixer_roster_, scene_item_properties_, source_media_inspector),
      performance_sampler_(ordinary_obs_, state_),
      critical_controls_(
          obs, state_, audit, &obs_request_arbiter_, [this] { request_reconcile(); }),
      command_processor_(ordinary_obs_, state_, audit, [this] { request_reconcile(); })
{
  if (thumbnail_capture && thumbnail_encoder)
    thumbnails_ =
        std::make_unique<ThumbnailService>(state_, *thumbnail_capture, *thumbnail_encoder);
}

GatewayRuntime::~GatewayRuntime()
{
  stop();
}

void GatewayRuntime::start()
{
  std::lock_guard lifecycle_lock(lifecycle_mutex_);
  if (running_)
    return;
  stopping_ = false;
  reconcile_requested_ = false;
  observed_recording_pause_available_ = obs_.recording_pause_available();

  obs_.on_event(
      [this](const NormalizedObsEvent& event)
      {
        if (event.type == "ExitStarted")
          lifecycle_.observe_obs_exit_started();

        if (auto input_names = read_audio_meter_input_names(event))
        {
          if (audio_mixer_roster_.observe_telemetry(*input_names))
            request_reconcile();
        }

        if (event.type == "StudioModeStateChanged")
        {
          request_reconcile();
        }

        if (event.type == "SceneListChanged" || event.type == "SceneCreated" ||
            event.type == "SceneRemoved" || event.type == "SceneNameChanged" ||
            event.type == "SceneItemCreated" || event.type == "SceneItemRemoved" ||
            event.type == "SceneItemListReindexed" || event.type == "InputCreated" ||
            event.type == "InputRemoved" || event.type == "InputNameChanged" ||
            event.type == "CurrentSceneCollectionChanged")
          request_reconcile();

        state_.apply_obs_event(event.type, event.data);
        if (event.type == "SceneItemEnableStateChanged" && !state_.fresh())
          request_reconcile();
      });

  obs_.on_connection(
      [this](bool connected, std::string_view version)
      {
        if (thumbnails_)
          thumbnails_->invalidate();
        state_.set_obs_connection(connected, std::string(version));
        critical_controls_.observe_obs_connection(connected);
        lifecycle_.observe_obs_connection(connected);
        if (connected)
        {
          request_reconcile();
        }
      });

  try
  {
    obs_.start();
    if (obs_.backend_info().kind == ProductionBackendKind::companion_teleprompter)
      reconcile();
    else
    {
      critical_controls_.start();
      reconciliation_thread_ = std::thread([this] { reconciliation_loop(); });
    }
    running_ = true;
  }
  catch (...)
  {
    stopping_ = true;
    critical_controls_.stop();
    obs_.stop();
    throw;
  }
}

void GatewayRuntime::stop()
{
  std::lock_guard lifecycle_lock(lifecycle_mutex_);
  if (!running_.exchange(false))
    return;
  stopping_ = true;

  obs_.stop();

  if (reconciliation_thread_.joinable())
  {
    reconciliation_thread_.join();
  }

  critical_controls_.stop();
  state_.set_obs_connection(false, {});
}

bool GatewayRuntime::running() const noexcept
{
  return running_.load();
}

StateStore& GatewayRuntime::state() noexcept
{
  return state_;
}

IObsAdapter& GatewayRuntime::obs() noexcept
{
  return obs_;
}

AuditStore& GatewayRuntime::audit() noexcept
{
  return audit_;
}

ObsProfileCatalogResult GatewayRuntime::obs_profiles(const Device& device)
{
  return obs_configuration_.profiles(device);
}

ObsSceneCollectionCatalogResult GatewayRuntime::obs_scene_collections(const Device& device)
{
  return obs_configuration_.scene_collections(device);
}

ObsConfigurationSelectionOutcome
GatewayRuntime::select_obs_profile(const Device& device, const ObsProfileSelection& selection)
{
  return obs_configuration_.select_profile(device, selection);
}

ObsConfigurationSelectionOutcome
GatewayRuntime::select_obs_scene_collection(const Device& device,
                                            const ObsSceneCollectionSelection& selection)
{
  return obs_configuration_.select_scene_collection(device, selection);
}

json GatewayRuntime::execute(const Device& device, std::string_view action, const json& body)
{
  return command_processor_.execute(device, action, body);
}

CriticalSubmitResult GatewayRuntime::submit_critical_action(const Device& device, const json& body)
{
  return critical_controls_.submit(device, body);
}

CriticalSubmitResult GatewayRuntime::acknowledge_critical_block(const Device& device,
                                                                const json& body)
{
  return critical_controls_.acknowledge_block(device, body);
}

GatewayRuntime::TransitionMutationResult
GatewayRuntime::set_current_transition(const Device& device, const json& body)
{
  return mutate_transition(device, body, true);
}

GatewayRuntime::TransitionMutationResult
GatewayRuntime::set_default_transition(const Device& device, const json& body)
{
  const auto current_sequence = state_.revision();
  if (device.role < Role::operator_)
    return {403, {{"status", "rejected"}, {"reason", "permission-required"}}};
  if (!state_.fresh())
  {
    return {503,
            {{"status", "rejected"},
             {"reason", "backend-unavailable"},
             {"currentStateSequence", current_sequence}}};
  }
  if (!ordinary_obs_.backend_info().capabilities.default_transition_configuration_available)
  {
    return {409,
            {{"status", "rejected"},
             {"reason", "capability-unavailable"},
             {"detail", "backend-api-unavailable"},
             {"currentStateSequence", current_sequence}}};
  }
  return mutate_transition(device, body, true);
}

GatewayRuntime::TransitionMutationResult GatewayRuntime::configure_transition(const Device& device,
                                                                              const json& body)
{
  return mutate_transition(device, body, false);
}

GatewayRuntime::TransitionMutationResult
GatewayRuntime::mutate_transition(const Device& device, const json& body, bool select_as_current)
{
  const auto current_sequence = state_.revision();
  if (device.role < Role::operator_)
  {
    return {403, {{"status", "rejected"}, {"reason", "permission-required"}}};
  }
  if (!state_.fresh())
  {
    return {503,
            {{"status", "rejected"},
             {"reason", "backend-unavailable"},
             {"currentStateSequence", current_sequence}}};
  }
  const auto expected = body.find("expectedStateSequence");
  if (expected == body.end() || !expected->is_number_integer() ||
      expected->get<std::int64_t>() < 0 ||
      static_cast<std::uint64_t>(expected->get<std::int64_t>()) != current_sequence)
  {
    return {409,
            {{"status", "rejected"},
             {"reason", "resource-stale"},
             {"currentStateSequence", current_sequence}}};
  }
  const auto transition_id = body.find("transitionId");
  if (transition_id == body.end() || !transition_id->is_string() ||
      transition_id->get<std::string>().empty())
  {
    return {422,
            {{"status", "rejected"},
             {"reason", "invalid-command-state"},
             {"currentStateSequence", current_sequence}}};
  }

  const auto selected_transition = state_.transition(transition_id->get<std::string>());
  if (!selected_transition)
  {
    return {404,
            {{"status", "rejected"},
             {"reason", "resource-stale"},
             {"currentStateSequence", current_sequence}}};
  }

  std::optional<std::uint32_t> requested_duration;
  if (const auto duration = body.find("durationMs"); duration != body.end())
  {
    if (!duration->is_number_integer() || duration->get<std::int64_t>() < 0)
    {
      return {422,
              {{"status", "rejected"},
               {"reason", "invalid-command-state"},
               {"currentStateSequence", current_sequence}}};
    }
    const auto value = static_cast<std::uint32_t>(duration->get<std::int64_t>());
    if (value < 50 || value > 20'000)
    {
      return {422,
              {{"status", "rejected"},
               {"reason", "invalid-command-state"},
               {"currentStateSequence", current_sequence}}};
    }
    requested_duration = value;
  }
  if (!select_as_current && !requested_duration)
  {
    return {422,
            {{"status", "rejected"},
             {"reason", "invalid-command-state"},
             {"currentStateSequence", current_sequence}}};
  }

  try
  {
    json selection = {{"transitionName", selected_transition->name},
                      {"quickTransitionId", selected_transition->id}};
    if (ordinary_obs_.backend_info().capabilities.stable_command_identifiers_preferred &&
        !selected_transition->backend_id.empty())
      selection["transitionUuid"] = selected_transition->backend_id;
    if (select_as_current && !state_.current_transition(transition_id->get<std::string>()))
      ordinary_obs_.request("SetCurrentSceneTransition", selection);
    if (requested_duration)
    {
      selection["transitionDuration"] = *requested_duration;
      ordinary_obs_.request(select_as_current ? "SetCurrentSceneTransitionDuration"
                                              : "SetQuickTransitionDuration",
                            std::move(selection));
    }
    audit_.command(device.id,
                   select_as_current ? "set-current-transition" : "configure-quick-transition",
                   selected_transition->name,
                   "accepted",
                   "");
    request_reconcile();
    return {202,
            {{"status", "accepted"},
             {"transitionId", *transition_id},
             {"durationMs", requested_duration ? json(*requested_duration) : json(nullptr)}}};
  }
  catch (const std::exception& error)
  {
    audit_.command(device.id,
                   select_as_current ? "set-current-transition" : "configure-quick-transition",
                   selected_transition->name,
                   "failed",
                   "");
    return {503,
            {{"status", "rejected"},
             {"reason", "backend-unavailable"},
             {"message", std::string(error.what()).substr(0, 240)},
             {"currentStateSequence", current_sequence}}};
  }
}

json GatewayRuntime::critical_action_status(std::string_view action_id, const Device& device) const
{
  return critical_controls_.status(action_id, device);
}

json GatewayRuntime::critical_queue_health() const
{
  return critical_controls_.queue_health();
}

bool GatewayRuntime::critical_controls_available() const noexcept
{
  return critical_controls_.available();
}

void GatewayRuntime::request_reconcile() noexcept
{
  reconcile_requested_ = true;
}

void GatewayRuntime::on_message(MessageSink sink)
{
  auto shared_sink = std::move(sink);
  command_processor_.set_message_sink(shared_sink);
  critical_controls_.set_message_sink(std::move(shared_sink));
}

void GatewayRuntime::on_shutdown_requested(GatewayLifecycle::ShutdownHandler handler)
{
  lifecycle_.set_shutdown_handler(std::move(handler));
}

std::string GatewayRuntime::last_error() const
{
  std::lock_guard lock(error_mutex_);
  return last_error_;
}

ThumbnailResult GatewayRuntime::source_thumbnail(std::string_view source_id,
                                                 std::uint32_t maximum_width,
                                                 std::uint32_t maximum_height,
                                                 std::string_view if_none_match)
{
  if (!thumbnails_)
  {
    ThumbnailResult unavailable;
    unavailable.status = ThumbnailResultStatus::unavailable;
    unavailable.reason = "helper_unavailable";
    return unavailable;
  }
  return thumbnails_->get_source(source_id, maximum_width, maximum_height, if_none_match);
}

ThumbnailResult GatewayRuntime::scene_thumbnail(std::string_view scene_id,
                                                std::uint32_t maximum_width,
                                                std::uint32_t maximum_height,
                                                std::string_view if_none_match)
{
  if (!thumbnails_)
  {
    ThumbnailResult unavailable;
    unavailable.status = ThumbnailResultStatus::unavailable;
    unavailable.reason = "helper_unavailable";
    return unavailable;
  }
  return thumbnails_->get_scene(scene_id, maximum_width, maximum_height, if_none_match);
}

json GatewayRuntime::thumbnail_diagnostics() const
{
  return thumbnails_ ? thumbnails_->diagnostics() : json{{"available", false}};
}

bool GatewayRuntime::thumbnails_available() const noexcept
{
  return thumbnails_ && thumbnails_->available();
}

json GatewayRuntime::audio_mixer_diagnostics() const
{
  return audio_mixer_roster_.diagnostics();
}

json GatewayRuntime::scene_item_properties_diagnostics() const
{
  return scene_item_properties_.diagnostics();
}

void GatewayRuntime::reconciliation_loop()
{
  auto next_periodic_reconciliation = Clock::now();
  auto next_performance_sample = Clock::now();

  while (!stopping_)
  {
    lifecycle_.poll();
    if (audio_mixer_roster_.refresh())
      request_reconcile();
    if (scene_item_properties_.refresh())
      request_reconcile();
    const bool recording_pause_available = obs_.recording_pause_available();
    if (recording_pause_available !=
        observed_recording_pause_available_.exchange(recording_pause_available))
      request_reconcile();
    const bool periodic_reconciliation_due = Clock::now() >= next_periodic_reconciliation;
    const bool explicitly_requested = reconcile_requested_.exchange(false);

    if (obs_.connected() && (explicitly_requested || periodic_reconciliation_due))
    {
      reconcile();
      next_periodic_reconciliation = Clock::now() + std::chrono::seconds(30);
    }

    if (obs_.connected() && obs_.backend_info().capabilities.performance &&
        Clock::now() >= next_performance_sample)
    {
      try
      {
        performance_sampler_.sample();
      }
      catch (const std::exception& error)
      {
        set_last_error("Performance sampling failed: " + std::string(error.what()).substr(0, 200));
      }
      catch (...)
      {
        set_last_error("Unexpected production performance sampling failure");
      }
      next_performance_sample =
          Clock::now() + std::chrono::milliseconds(performance_update_interval_ms);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void GatewayRuntime::reconcile()
{
  std::unique_lock lock(reconciliation_mutex_, std::try_to_lock);
  if (!lock.owns_lock())
  {
    return;
  }

  try
  {
    state_.set_gateway_state("starting");
    const auto observation_generation = state_.source_observation_generation();
    const auto baseline = reconciler_.build_baseline();
    ProductionCatalogSnapshot catalogs;
    catalogs.canvas = obs_.canvas_state();
    catalogs.collections = obs_.scene_collection_catalog();
    if (!state_.replace_baseline(baseline, std::move(catalogs), observation_generation))
    {
      request_reconcile();
      return;
    }
    set_last_error({});
  }
  catch (const std::exception& error)
  {
    set_last_error(std::string(error.what()).substr(0, 240));
    state_.set_obs_connection(false);
  }
  catch (...)
  {
    set_last_error("Unexpected OBS reconciliation failure");
    state_.set_obs_connection(false);
  }
}

void GatewayRuntime::set_last_error(std::string message)
{
  std::lock_guard lock(error_mutex_);
  last_error_ = std::move(message);
}

json GatewayRuntime::select_collection(const Device& device,
                                       const ProductionCollectionSelection& selection)
{
  return command_processor_.select_collection(device, selection, obs_, critical_controls_);
}

} // namespace studio_remote
