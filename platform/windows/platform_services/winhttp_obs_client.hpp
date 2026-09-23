#pragma once
#include "windows_obs_quick_transition_control.hpp"
#include "windows_obs_runtime_capabilities.hpp"
#include "windows_obs_websocket.hpp"
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <studio_remote/common/types.hpp>
#include <studio_remote/obs/obs_adapter.hpp>
#include <studio_remote/obs/obs_connection_callbacks.hpp>
#include <studio_remote/obs/obs_diagnostic_events.hpp>
#include <studio_remote/obs/obs_pending_requests.hpp>
#include <studio_remote/obs/obs_quick_transition_identities.hpp>
#include <thread>

namespace studio_remote::windows
{
class ObsClient final : public IObsAdapter
{
public:
  using DiagnosticEventHandler = ObsDiagnosticEvents::Handler;

  ObsClient(std::string host,
            uint16_t port,
            std::string password,
            std::optional<std::filesystem::path> configuration_root = std::nullopt);
  ~ObsClient();
  void start() override;
  void stop() override;
  void reconfigure(std::string host, uint16_t port, std::string password);
  bool connected() const override
  {
    return connected_;
  }
  std::string last_error() const override
  {
    std::lock_guard lock(error_mutex_);
    return last_error_;
  }
  json request(std::string_view request_type,
               json request_data = {},
               std::source_location caller = std::source_location::current(),
               std::string_view reason = {}) override;
  ProductionBackendInfo backend_info() const override
  {
    auto info = ProductionBackendInfo{};
    info.capabilities.audio_input_positive_gain = true;
    info.capabilities.authoritative_control_verification = true;
    info.capabilities.explicit_start_stop_for_record = true;
    info.capabilities.explicit_start_stop_for_stream = true;
    info.capabilities.performance = true;
    return info;
  }
  void trigger_hotkey(const ProductionHotkeyTrigger& trigger) override;
  std::optional<std::vector<ProductionHotkeyDefinition>> hotkeys() override;
  bool hotkey_catalog_supported() const noexcept override
  {
    return true;
  }
  bool recording_pause_available() const override
  {
    return runtime_capabilities_.recording_pause_available();
  }
  void on_diagnostic_event(DiagnosticEventHandler handler);
  void on_event(EventCallback handler) override
  {
    callbacks_.on_event(std::move(handler));
  }
  void on_connection(ConnectionCallback handler) override
  {
    callbacks_.on_connection(std::move(handler));
  }

private:
  WindowsObsRuntimeCapabilities runtime_capabilities_;
  void run();
  void read_connected_messages();
  bool connect_once(std::uint64_t configuration_revision, std::optional<Clock::time_point>& opened);
  void emit_connection(bool connected, std::string_view version);
  void emit_event(const NormalizedObsEvent& event);
  std::uint32_t reconnect_random_sample();
  std::optional<std::string> receive_message(std::chrono::milliseconds timeout = {});
  bool send_message(std::string_view message);
  void dispatch(std::string_view wire_message);
  json request_wire(std::string_view type,
                    json data,
                    std::source_location caller,
                    std::string_view reason);
  void map_response(std::string_view type, json& response);
  void close_socket();
  bool set_error(std::string message)
  {
    std::lock_guard lock(error_mutex_);
    const bool changed = last_error_ != message;
    last_error_ = std::move(message);
    return changed;
  }

  const std::optional<std::filesystem::path> configuration_root_;
  std::string host_, password_;
  uint16_t port_;
  std::atomic<bool> stopping_{false}, connected_{false};
  std::atomic<std::uint64_t> configuration_revision_{0};
  std::thread thread_;
  std::atomic<DWORD> reader_id_{0};
  WindowsObsWebSocket socket_;
  mutable std::mutex configuration_mutex_, reconnect_mutex_;
  std::condition_variable reconnect_cv_;
  mutable std::mutex error_mutex_;
  std::string last_error_;
  ObsDiagnosticEvents diagnostics_;
  ObsQuickTransitionIdentities quick_transition_identities_;
  WindowsObsQuickTransitionControl quick_transition_control_;
  ObsPendingRequests pending_;
  ObsConnectionCallbacks callbacks_;
};
} // namespace studio_remote::windows
