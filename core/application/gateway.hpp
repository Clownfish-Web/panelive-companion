#pragma once

#include "audio_mixer_roster.hpp"
#include "critical_live_control_service.hpp"
#include "gateway_command_processor.hpp"
#include "gateway_lifecycle.hpp"
#include "obs_configuration_service.hpp"
#include "obs_state_reconciler.hpp"
#include "performance_sampler.hpp"
#include "scene_item_properties.hpp"
#include "thumbnail_service.hpp"

#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/credentials/credential_store.hpp>
#include <studio_remote/obs/obs_adapter.hpp>
#include <studio_remote/obs/obs_request_arbiter.hpp>
#include <studio_remote/state/state_store.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace studio_remote
{

class GatewayRuntime
{
public:
  using MessageSink = GatewayCommandProcessor::MessageSink;
  struct TransitionMutationResult
  {
    int http_status = 422;
    json body;
  };

  GatewayRuntime(GatewayIdentity identity,
                 IObsAdapter& obs,
                 AuditStore& audit,
                 IThumbnailCapture* thumbnail_capture = nullptr,
                 IThumbnailEncoder* thumbnail_encoder = nullptr,
                 IAudioMixerRosterSource* audio_mixer_source = nullptr,
                 ISceneItemPropertiesSource* scene_item_properties_source = nullptr,
                 ISourceMediaInspector* source_media_inspector = nullptr);
  ~GatewayRuntime();

  GatewayRuntime(const GatewayRuntime&) = delete;
  GatewayRuntime& operator=(const GatewayRuntime&) = delete;

  void start();
  void stop();
  bool running() const noexcept;

  StateStore& state() noexcept;
  IObsAdapter& obs() noexcept;
  AuditStore& audit() noexcept;

  ObsProfileCatalogResult obs_profiles(const Device& device);
  ObsSceneCollectionCatalogResult obs_scene_collections(const Device& device);
  ObsConfigurationSelectionOutcome select_obs_profile(const Device& device,
                                                      const ObsProfileSelection& selection);
  ObsConfigurationSelectionOutcome
  select_obs_scene_collection(const Device& device, const ObsSceneCollectionSelection& selection);

  json execute(const Device& device, std::string_view action, const json& body);
  json select_collection(const Device& device, const ProductionCollectionSelection& selection);
  CriticalSubmitResult submit_critical_action(const Device& device, const json& body);
  CriticalSubmitResult acknowledge_critical_block(const Device& device, const json& body);
  TransitionMutationResult set_current_transition(const Device& device, const json& body);
  TransitionMutationResult set_default_transition(const Device& device, const json& body);
  TransitionMutationResult configure_transition(const Device& device, const json& body);
  json critical_action_status(std::string_view action_id, const Device& device) const;
  json critical_queue_health() const;
  bool critical_controls_available() const noexcept;
  void request_reconcile() noexcept;
  void on_message(MessageSink sink);
  void on_shutdown_requested(GatewayLifecycle::ShutdownHandler handler);
  std::string last_error() const;
  ThumbnailResult source_thumbnail(std::string_view source_id,
                                   std::uint32_t maximum_width,
                                   std::uint32_t maximum_height,
                                   std::string_view if_none_match = {});
  ThumbnailResult scene_thumbnail(std::string_view scene_id,
                                  std::uint32_t maximum_width,
                                  std::uint32_t maximum_height,
                                  std::string_view if_none_match = {});
  json thumbnail_diagnostics() const;
  bool thumbnails_available() const noexcept;
  json audio_mixer_diagnostics() const;
  json scene_item_properties_diagnostics() const;

private:
  TransitionMutationResult
  mutate_transition(const Device& device, const json& body, bool select_as_current);
  void reconciliation_loop();
  void reconcile();
  void set_last_error(std::string message);

  IObsAdapter& obs_;
  ObsRequestArbiter obs_request_arbiter_;
  LaneObsAdapter ordinary_obs_;
  AuditStore& audit_;
  StateStore state_;
  GatewayLifecycle lifecycle_;
  AudioMixerRoster audio_mixer_roster_;
  SceneItemProperties scene_item_properties_;
  ObsConfigurationService obs_configuration_;
  ObsStateReconciler reconciler_;
  PerformanceSampler performance_sampler_;
  CriticalLiveControlService critical_controls_;
  GatewayCommandProcessor command_processor_;
  std::unique_ptr<ThumbnailService> thumbnails_;

  std::atomic<bool> stopping_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> reconcile_requested_{false};
  std::atomic<bool> observed_recording_pause_available_{false};
  std::thread reconciliation_thread_;
  std::mutex lifecycle_mutex_;
  std::mutex reconciliation_mutex_;

  mutable std::mutex error_mutex_;
  std::string last_error_;
};

} // namespace studio_remote
