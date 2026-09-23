#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <studio_remote/obs/obs_adapter.hpp>
#include <studio_remote/streamlabs/streamlabs_state.hpp>
#include <studio_remote/streamlabs/streamlabs_transport.hpp>
#include <thread>

namespace studio_remote::streamlabs
{

class StreamlabsAdapter final : public IObsAdapter
{
public:
  explicit StreamlabsAdapter(std::unique_ptr<IStreamlabsTransport> transport);
  ~StreamlabsAdapter() override;

  /// Replaces the connection after settings commit; retains runtime callbacks and ownership.
  void reconfigure(std::unique_ptr<IStreamlabsTransport> transport);
  void start() override;
  void stop() override;
  bool connected() const override;
  json request(std::string_view type,
               json data = {},
               std::source_location caller = std::source_location::current(),
               std::string_view reason = {}) override;
  json request_current(std::string_view type,
                       json data = {},
                       std::source_location caller = std::source_location::current(),
                       std::string_view reason = {}) override;
  void on_event(EventCallback callback) override;
  void on_connection(ConnectionCallback callback) override;
  ProductionBackendInfo backend_info() const override;
  std::string last_error() const override;
  std::optional<ProductionCanvasState> canvas_state() const override;
  std::optional<ProductionSceneCollectionCatalog> scene_collection_catalog() override;
  bool select_scene_collection(std::string_view id, std::uint64_t expected_revision) override;
  std::optional<std::vector<ProductionHotkeyDefinition>> hotkeys() override;
  bool hotkey_catalog_supported() const noexcept override;
  void trigger_hotkey(const ProductionHotkeyTrigger& trigger) override;

private:
  enum class ControlKind
  {
    program_scene,
    preview_scene,
    scene_item_visibility,
    audio_mute,
    audio_volume,
    recording,
    streaming,
    studio_transition
  };

  struct ControlObservation
  {
    ControlKind kind = ControlKind::program_scene;
    std::string resource_id;
    std::string desired_scene_id;
    bool desired_boolean = false;
    double desired_volume_db = 0.0;
  };

  void run();
  void refresh(bool provider_observation = true);
  void refresh_program_identity();
  void observe_event(std::string_view resource, const json& data);
  void publish(State replacement,
               bool provider_observation,
               std::optional<std::size_t> expected_output_revision = std::nullopt);
  json read_request(std::string_view type, const json& data, const State& state) const;
  ControlObservation mutation(std::string_view type, const json& data, const State& state);
  bool observe_control(const ControlObservation& observation);
  void settle_control(const ControlObservation& observation);
  void request_refresh();

  std::unique_ptr<IStreamlabsTransport> transport_;
  mutable std::mutex mutex_;
  mutable std::mutex transport_mutex_;
  std::mutex command_mutex_;
  std::uint64_t collection_revision_ = 0;
  std::chrono::steady_clock::time_point next_program_identity_probe_;
  std::condition_variable wake_;
  std::thread thread_;
  State state_;
  EventCallback event_callback_;
  ConnectionCallback connection_callback_;
  std::string last_error_;
  bool connected_ = false;
  bool refresh_requested_ = false;
  std::size_t output_event_revision_ = 0;
  std::size_t consecutive_worker_failures_ = 0;
  std::atomic<bool> stopping_{false};
};

} // namespace studio_remote::streamlabs
