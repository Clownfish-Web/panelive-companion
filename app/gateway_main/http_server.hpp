#pragma once
#include "../../core/application/gateway.hpp"
#include "../../core/application/hotkey_service.hpp"
#include "../../core/transport/libdatachannel_transport.hpp"
#include "client_event_protocol_registry.hpp"
#include "countdown_event_protocol.hpp"
#include "http_callback_lifetime.hpp"
#include "http_tls_context.hpp"
#include "media_session_reset_registry.hpp"
#include "webrtc_recovery_coordinator.hpp"
#include "websocket_frame_router.hpp"
#include <CivetServer.h>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <studio_remote/credentials/pairing_manager.hpp>
#include <studio_remote/events/client_identification.hpp>
#include <studio_remote/observability/endurance_event_log.hpp>
#include <studio_remote/provider/telestrator.hpp>
#include <studio_remote/teleprompter/repository.hpp>
#include <studio_remote/transport/request_admission.hpp>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace studio_remote
{
enum class HttpRequestMethod;
namespace tally
{
class PublicationService;
}
struct HttpConfig
{
  std::uint16_t port = 7443;
  std::string listening_ports;
  std::filesystem::path certificate;
  std::filesystem::path private_key;
  std::filesystem::path error_log;
  GatewayIdentity identity;
  HttpTlsInitializer initialize_tls_context{};
};

struct ProviderSession
{
  std::string id;
  GatewayRuntime* runtime = nullptr;
  WebRtcHub* webrtc = nullptr;
  ITelestratorDiscovery* telestrator = nullptr;
  std::string event_scope_id{};
  std::shared_ptr<ClientEventAuthority> event_authority = std::make_shared<ClientEventAuthority>();
};

class GatewayHttpServer final : private CivetHandler, private CivetWebSocketHandler
{
public:
  enum class RoleChangeRequestDisposition
  {
    unavailable,
    pending,
    approved
  };

  GatewayHttpServer(HttpConfig config,
                    std::vector<ProviderSession> provider_sessions,
                    PairingManager& pairing,
                    HotkeyService& hotkeys,
                    EnduranceEventLog* endurance_log = nullptr,
                    teleprompter::IRepository* teleprompter = nullptr,
                    tally::PublicationService* remote_tallies = nullptr);
  ~GatewayHttpServer();
  using RoleChangeRequestHandler = std::function<RoleChangeRequestDisposition(
      const Device&, std::string_view request_id, Role requested_role)>;
  using GlobalSettingChangeRequestHandler = std::function<RoleChangeRequestDisposition(
      const Device&, std::string_view request_id, std::string_view setting_id, const json& value)>;

  void start();
  void stop();
  void configure(HttpConfig config);
  bool running() const noexcept;
  void broadcast(const json& message);
  void reinitialize_media_sessions(std::string_view production_provider,
                                   std::uint32_t capture_generation);
  void on_role_change_requested(RoleChangeRequestHandler handler);
  /// Invoked after credential invalidation, outside transport locks, before session removal.
  void on_client_removed(std::function<void(std::string_view)> handler);
  void on_global_setting_change_requested(GlobalSettingChangeRequestHandler handler);
  void apply_role_changes(const std::vector<DeviceRoleChange>& changes);
  void disconnect_device(std::string_view device_id);
  /// Local management only. Delivery is best effort to the current ready WSS connections.
  void identify_client(std::string_view device_id, ClientIdentificationOptions options);
  void identify_all_clients(ClientIdentificationOptions options);
  std::vector<ClientIdentification> client_identifications();
  void publish_capabilities(std::string_view production_provider);
  // Called by owned application/HTTP work before server teardown. GET recovers missed events.
  void publish_tally_publication(std::string_view device_id, std::string_view publication_id);
  void complete_role_change_request(std::string_view device_id,
                                    std::string_view request_id,
                                    Role requested_role,
                                    bool approved);
  void complete_global_setting_change_request(std::string_view device_id,
                                              std::string_view request_id,
                                              bool approved);
  std::vector<Device> connected_devices() const;
  json diagnostics() const;
  /// Periodic local maintenance; does not expire active WebSocket or media sessions.
  void expire_client_requests(Clock::time_point now);

private:
  struct GenericCommandOutcome
  {
    int http_status = 422;
    json body;
  };

  GenericCommandOutcome execute_role_request(const Device& device,
                                             std::string_view command_id,
                                             Role requested_role,
                                             std::uint64_t current_sequence);
  GenericCommandOutcome
  execute_role_extension(const Device& device, std::string_view request_id, Role requested_role);

  struct AuthorizationResult
  {
    std::optional<Device> device;
    std::string failure_reason;
  };

  bool handleGet(CivetServer*, mg_connection*) override;
  bool handlePost(CivetServer*, mg_connection*) override;
  bool handlePut(CivetServer*, mg_connection*) override;
  bool handleDelete(CivetServer*, mg_connection*) override;
  bool handle_tally_request(mg_connection*, HttpRequestMethod);
  void deliver_tally_request(mg_connection*, const Device&, HttpRequestMethod);
  void reply_authentication_failure(mg_connection*, std::string_view reason);
  bool handleConnection(CivetServer*, const mg_connection*) override;
  void handleReadyState(CivetServer*, mg_connection*) override;
  bool handleData(CivetServer*, mg_connection*, int, char*, size_t) override;
  void handleClose(CivetServer*, const mg_connection*) override;
  AuthorizationResult authorize(const mg_connection*, std::string_view transport) const;
  static int initialize_civet_tls(void*, void*) noexcept;
  static int handle_civet_log(const mg_connection*, const char*) noexcept;
  json read_json(mg_connection*) const;
  void reply(mg_connection*, int, const json&, std::uint32_t retry_after_ms = 0) const;
  void
  reply_rate_limited(mg_connection*, std::string_view scope, std::uint32_t retry_after_ms) const;
  void reply_websocket_rate_limited(mg_connection*, std::uint32_t retry_after_ms) const;
  void reply_thumbnail(mg_connection*, const ThumbnailResult&, std::string_view error) const;
  void
  handle_thumbnail(mg_connection*, const Device&, std::string_view path, ThumbnailTarget target);
  json capabilities(const Device&) const;
  json capabilities(const Device&, const ProductionCatalogSnapshot&) const;
  json backend_status(const Device&) const;
  GenericCommandOutcome execute_generic_command(const Device&, const json& envelope);
  void handle_websocket_text(mg_connection*, ClientEventConnection&, std::string_view text);
  void handle_websocket_resync(const Device&, ClientEventConnection&);
  void handle_websocket_command(const Device&, ClientEventConnection&, json envelope);
  void publish_replacements();
  void publish_replacements(const Device& device);
  json state_replacement(const Device&) const;
  json composition_replacement(const Device&) const;
  json transition_replacement(const Device&) const;
  std::optional<json> streaming_destination_replacement(const Device&) const;
  std::vector<json> layer_replacements(const Device&) const;
  json layer_replacement(const Device&, std::string_view composition_id) const;
  json audio_input_replacement(const Device&) const;
  bool write_text(mg_connection*, std::string_view);
  std::string uri(const mg_connection*) const;
  RequestAdmission::Result permit_public(const mg_connection*, RequestScope scope);
  RequestAdmission::Result permit_device(const Device&, RequestScope scope);
  void queue_message(std::string text,
                     std::optional<std::string> device_id,
                     std::optional<std::string> production_provider = std::nullopt,
                     ClientDeliveryPriority priority = ClientDeliveryPriority::state);
  void queue_message_locked(std::string text,
                            const std::optional<std::string>& device_id,
                            const std::optional<std::string>& production_provider,
                            ClientDeliveryPriority priority);
  void request_cleanup_loop();
  void publish_bus_replacements(std::string_view provider);
  void publish_output_replacements(std::string_view provider);
  ProviderSession& provider_session(const Device&) const;
  void apply_role_change(const DeviceRoleChange& change);
  bool open_client_events(mg_connection*, const Device&);
  void publish_client_identification(std::optional<std::string_view> device_id,
                                     ClientIdentificationOptions options);
  struct ClientIdentificationTarget
  {
    std::uint64_t connection_id;
    ClientIdentification client;
  };
  std::vector<ClientIdentificationTarget>
  client_identification_targets(std::optional<std::string_view> device_id);
  void refresh_event_authority(const Device&) const;
  void publish_teleprompter_session(std::string_view device_id,
                                    std::string_view session_id,
                                    bool position_only);

  HttpConfig config_;
  std::vector<ProviderSession> provider_sessions_;
  PairingManager& pairing_;
  HotkeyService& hotkeys_;
  EnduranceEventLog* endurance_log_ = nullptr;
  // Borrowed from composition; it outlives this server and its request workers.
  teleprompter::IRepository* teleprompter_ = nullptr;
  tally::PublicationService* remote_tallies_ = nullptr;
  CivetCallbacks callbacks_;
  std::unique_ptr<HttpTlsContext> tls_context_;
  std::unique_ptr<CivetServer> server_;
  mutable std::mutex sockets_mutex_;
  std::unordered_set<mg_connection*> sockets_;
  std::unordered_map<mg_connection*, Device> socket_devices_;
  // Bounded by ready event connections and confined to sockets_mutex_. Values are local labels.
  std::unordered_map<std::string, std::string> client_identification_codes_;
  std::uint32_t client_identification_sequence_ = 0;
  CountdownEventPolicy countdown_events_;
  ClientEventProtocolRegistry client_event_protocols_;
  ClientEventRelay client_events_;
  std::atomic<std::uint64_t> event_connection_sequence_{0};
  RequestAdmission request_admission_;
  // Guarded by sockets_mutex_. Retain even after revocation until the native close callback.
  // Declared after its borrowed admission owner so reservations are destroyed first.
  std::unordered_map<mg_connection*, std::unique_ptr<RequestAdmission::RequestReservation>>
      websocket_reservations_;
  std::mutex maintenance_mutex_;
  std::condition_variable maintenance_cv_;
  std::thread maintenance_thread_;
  std::atomic<bool> delivery_stopping_{true};
  // Registered providers retain the gate, but never prolong the server's lifetime.
  std::shared_ptr<HttpCallbackLifetime> state_callback_lifetime_ =
      std::make_shared<HttpCallbackLifetime>();
  std::size_t registered_state_subscriptions_ = 0;
  std::atomic<std::uint64_t> capability_revision_{1};
  std::mutex role_change_request_handler_mutex_;
  RoleChangeRequestHandler role_change_request_handler_;
  std::mutex client_removed_handler_mutex_;
  std::function<void(std::string_view)> client_removed_handler_;
  std::mutex global_setting_change_request_handler_mutex_;
  GlobalSettingChangeRequestHandler global_setting_change_request_handler_;
  std::mutex generic_command_cache_mutex_;
  struct GenericCommandAdmission
  {
    GatewayHttpServer& server;
    const std::string& key;
    bool admitted = false;
    ~GenericCommandAdmission();
  };
  std::unordered_set<std::string> generic_commands_in_flight_;
  Clock::time_point next_request_cleanup_{};
  std::unordered_map<std::string, std::pair<Clock::time_point, GenericCommandOutcome>>
      generic_command_cache_;
  struct PendingRoleRequest
  {
    // Extensions have request correlation but no typed-command operation ID.
    std::optional<std::string> command_operation_id;
    Role requested_role;
    Clock::time_point admitted_at = Clock::now();
  };
  std::unordered_map<std::string, PendingRoleRequest> role_request_operations_;
  struct PendingGlobalSettingRequest
  {
    std::string operation_id;
    Clock::time_point admitted_at = Clock::now();
  };
  std::unordered_map<std::string, PendingGlobalSettingRequest> global_setting_request_operations_;
  WebSocketFrameRouter websocket_frames_;
  MediaSessionResetRegistry media_session_resets_;
  WebRtcRecoveryCoordinator webrtc_recovery_;
  mutable std::atomic<std::uint64_t> http_requests_{0};
  mutable std::atomic<std::uint64_t> http_responses_{0};
  mutable std::atomic<std::uint64_t> active_http_requests_{0};
  mutable std::atomic<std::uint64_t> maximum_active_http_requests_{0};
  mutable std::atomic<std::uint64_t> http_client_errors_{0};
  mutable std::atomic<std::uint64_t> http_server_errors_{0};
};
} // namespace studio_remote
