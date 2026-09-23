#pragma once

#include "macos_obs_connection.hpp"
#include "macos_obs_quick_transition_control.hpp"
#include "macos_obs_runtime_capabilities.hpp"

#include <filesystem>
#include <optional>
#include <studio_remote/obs/obs_diagnostic_events.hpp>
#include <studio_remote/obs/obs_quick_transition_identities.hpp>

namespace studio_remote::macos
{
/// Native composition inputs. Explicit paths/names also allow isolated adapter-boundary tests.
struct ObsClientInputs
{
  // Absent resolves the native root on each catalog read; an explicit empty root disables files.
  std::optional<std::filesystem::path> configuration_root;
  std::string transition_mapping = ipc_names::quick_transition_control;
  std::string capability_mapping = ipc_names::runtime_capabilities;
  ObsConnection::SocketFactory sockets;
};

/// OBS protocol adapter over the owned connection and helper IPC. Lifecycle and callback rules
/// match ObsConnection. The catalog mutex protects only cached IDs, never network or helper waits.
class ObsClient final : public IObsAdapter
{
public:
  using DiagnosticEventHandler = ObsDiagnosticEvents::Handler;
  ObsClient(std::string host, std::uint16_t port, std::string password);
  ObsClient(std::string host, std::uint16_t port, std::string password, ObsClientInputs inputs);
  ~ObsClient() override;
  void start() override;
  void stop() override;
  void reconfigure(std::string host, std::uint16_t port, std::string password);
  bool connected() const override;
  std::string last_error() const override;
  json request(std::string_view type,
               json data = {},
               std::source_location caller = std::source_location::current(),
               std::string_view reason = {}) override;
  void on_diagnostic_event(DiagnosticEventHandler callback);
  void on_event(EventCallback callback) override;
  void on_connection(ConnectionCallback callback) override;
  ProductionBackendInfo backend_info() const override;
  bool recording_pause_available() const override;
  void trigger_hotkey(const ProductionHotkeyTrigger& trigger) override;
  std::optional<std::vector<ProductionHotkeyDefinition>> hotkeys() override;
  bool hotkey_catalog_supported() const noexcept override;

private:
  json request_wire(std::string_view type,
                    json data,
                    std::source_location caller,
                    std::string_view reason);
  void map_response(std::string_view type, json& response);
  const std::optional<std::filesystem::path> configuration_root_;
  MacosObsQuickTransitionControl quick_transition_control_;
  MacosObsRuntimeCapabilities capabilities_;
  ObsQuickTransitionIdentities quick_transition_identities_;
  ObsDiagnosticEvents diagnostics_;
  ObsConnection connection_;
};
} // namespace studio_remote::macos
