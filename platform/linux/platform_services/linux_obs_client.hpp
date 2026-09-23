#pragma once

#include "linux_obs_quick_transition_control.hpp"
#include "linux_obs_runtime_capabilities.hpp"

#include <condition_variable>
#include <deque>
#include <rtc/rtc.hpp>
#include <studio_remote/obs/obs_adapter.hpp>
#include <unordered_map>

namespace studio_remote::linux
{
class ObsClient final : public IObsAdapter
{
public:
  using DiagnosticEventHandler = std::function<void(std::string_view, json)>;

  ObsClient(std::string host, std::uint16_t port, std::string password);
  ~ObsClient() override;
  void start() override;
  void stop() override;
  void reconfigure(std::string host, std::uint16_t port, std::string password);
  bool connected() const override;
  std::string last_error() const override;
  json request(std::string_view request_type,
               json request_data = {},
               std::source_location caller = std::source_location::current(),
               std::string_view reason = {}) override;
  ProductionBackendInfo backend_info() const override;
  void trigger_hotkey(const ProductionHotkeyTrigger& trigger) override;
  std::optional<std::vector<ProductionHotkeyDefinition>> hotkeys() override;
  bool hotkey_catalog_supported() const noexcept override;
  bool recording_pause_available() const override;
  void on_event(EventCallback handler) override;
  void on_connection(ConnectionCallback handler) override;
  void on_diagnostic_event(DiagnosticEventHandler handler);

private:
  LinuxObsRuntimeCapabilities runtime_capabilities_;
  LinuxObsQuickTransitionControl quick_transition_control_;
  void run();
  bool connect_once(std::uint64_t configuration_revision);
  std::optional<std::string> receive_message(std::chrono::milliseconds timeout);
  bool send_message(std::string message);
  void dispatch(const json& message);
  void close_socket();
  std::string authentication(const json& data, std::string_view password) const;
  void set_error(std::string message);
  void emit_diagnostic_event(std::string_view event, json details) const;

  mutable std::mutex mutex_;
  std::condition_variable socket_cv_;
  std::string host_;
  std::string password_;
  std::uint16_t port_;
  std::string last_error_;
  std::shared_ptr<rtc::WebSocket> socket_;
  std::deque<std::string> messages_;
  bool socket_open_ = false;
  bool socket_closed_ = true;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> connected_{false};
  std::atomic<std::uint64_t> configuration_revision_{0};
  std::thread thread_;
  mutable std::mutex pending_mutex_;
  mutable std::mutex quick_transition_mutex_;
  std::unordered_map<std::string, std::string> quick_transition_backend_names_;
  std::unordered_map<std::string, std::int32_t> quick_transition_native_ids_;
  std::unordered_map<std::string, std::promise<json>> pending_;
  EventCallback event_handler_;
  ConnectionCallback connection_handler_;
  DiagnosticEventHandler diagnostic_event_handler_;
};
} // namespace studio_remote::linux
