#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <studio_remote/obs/obs_adapter.hpp>
#include <studio_remote/vmix/vmix_input_category_source.hpp>
#include <studio_remote/vmix/vmix_master_mute.hpp>
#include <studio_remote/vmix/vmix_state.hpp>
#include <studio_remote/vmix/vmix_streaming_destination_source.hpp>
#include <studio_remote/vmix/vmix_transport.hpp>
#include <unordered_map>
#include <vector>

namespace studio_remote::vmix
{

class VmixAdapter final : public IObsAdapter
{
public:
  explicit VmixAdapter(
      std::unique_ptr<IVmixTransport> transport,
      bool streaming_configured = false,
      std::unique_ptr<IVmixInputCategorySource> input_categories = {},
      std::unique_ptr<IVmixStreamingDestinationSource> streaming_destinations = {});
  ~VmixAdapter() override;

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
  void trigger_hotkey(const ProductionHotkeyTrigger& trigger) override;
  std::optional<std::vector<ProductionHotkeyDefinition>> hotkeys() override;
  bool hotkey_catalog_supported() const noexcept override
  {
    return true;
  }
  void write_input_snapshot(std::string_view input_id, std::string_view output_path);
  void reconfigure(std::unique_ptr<IVmixTransport> transport, bool streaming_configured);

private:
  struct LayerVisibilityConfirmation
  {
    std::string composition_key;
    std::uint32_t layer_index = 0;
    bool visible = true;
  };

  void set_master_mute(bool muted);
  void run();
  State refresh();
  void publish(State replacement, bool provider_observation = true);
  void publish_layer_visibility_confirmation(const json& data, State replacement);
  void apply_layer_visibility_confirmations(State& replacement, bool provider_observation);
  void apply_streaming_destination_activity(State& replacement, bool provider_observation);
  void record_streaming_mutation(std::string_view type, const json& data, const State& state);
  void publish_streaming_mutation(std::string_view type, State replacement);
  json read_request(std::string_view type, const json& data, const State& state) const;
  void mutation(std::string_view type, const json& data, const State& state);

  std::unique_ptr<IVmixTransport> transport_;
  std::unique_ptr<IVmixInputCategorySource> input_categories_;
  std::unique_ptr<IVmixStreamingDestinationSource> streaming_destinations_;
  std::mutex transport_mutex_;
  VmixMasterMute master_mute_; // Protected by transport_mutex_, never the state mutex.
  bool streaming_configured_ = false;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  State state_;
  std::vector<LayerVisibilityConfirmation> layer_visibility_confirmations_;
  std::unordered_map<std::uint32_t, bool> streaming_destination_activity_;
  std::optional<bool> pending_streaming_aggregate_;
  std::string last_error_;
  EventCallback event_callback_;
  ConnectionCallback connection_callback_;
  std::thread worker_;
  bool stopping_ = false;
  bool connected_ = false;
};

} // namespace studio_remote::vmix
