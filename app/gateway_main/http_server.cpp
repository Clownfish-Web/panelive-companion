#include "http_server.hpp"

#include "authentication_diagnostics.hpp"
#include "civet_user_context.hpp"
#include "device_name_protocol.hpp"
#include "device_role_protocol.hpp"
#include "generic_command_envelope.hpp"
#include "hotkey_protocol.hpp"
#include "http_authentication.hpp"
#include "http_request_policy.hpp"
#include "media_session_reset_protocol.hpp"
#include "obs_configuration_protocol.hpp"
#include "pairing_exchange_protocol.hpp"
#include "production_catalog_protocol.hpp"
#include "state_publication_policy.hpp"
#include "tally_publication_routes.hpp"
#include "teleprompter_protocol.hpp"
#include "teleprompter_session_protocol.hpp"
#include "telestrator_protocol.hpp"
#include "webrtc_recovery_protocol.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <studio_remote/common/client_request_lifetime.hpp>
#include <studio_remote/credentials/device_name.hpp>
#include <studio_remote/credentials/storage_busy.hpp>
#include <studio_remote/protocol/client_json.hpp>
#include <studio_remote/transport/webrtc_offer.hpp>

namespace studio_remote
{
namespace
{
constexpr std::size_t server_worker_threads = 64;
static_assert(RequestAdmission::maximum_websocket_connections < server_worker_threads);

struct HttpHeader
{
  std::string_view name;
  std::string_view value;
};

bool send_response_headers(mg_connection* connection,
                           int status,
                           std::string_view content_type,
                           std::size_t content_length,
                           std::string_view cache_control,
                           std::initializer_list<HttpHeader> additional_headers = {})
{
  const auto add_header = [connection](std::string_view name, std::string_view value)
  {
    if (name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      return false;
    return mg_response_header_add(
               connection, name.data(), value.data(), static_cast<int>(value.size())) == 0;
  };

  const auto content_length_text = std::to_string(content_length);
  if (mg_response_header_start(connection, status) != 0 ||
      (!content_type.empty() && !add_header("Content-Type", content_type)) ||
      !add_header("Content-Length", content_length_text) ||
      !add_header("Cache-Control", cache_control) ||
      !add_header("X-Content-Type-Options", "nosniff") ||
      !add_header("Strict-Transport-Security", "max-age=31536000") ||
      !add_header("Connection", "close"))
    return false;

  for (const auto& header : additional_headers)
    if (!add_header(header.name, header.value))
      return false;
  return mg_response_header_send(connection) == 0;
}

bool send_response_body(mg_connection* connection, const void* data, std::size_t size)
{
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::size_t offset = 0;
  while (offset < size)
  {
    const auto remaining = size - offset;
    const auto chunk =
        std::min(remaining, static_cast<std::size_t>(std::numeric_limits<int>::max()));
    const int written = mg_write(connection, bytes + offset, chunk);
    if (written <= 0 || static_cast<std::size_t>(written) > chunk)
      return false;
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

void add_string_field_if_present(json& destination, const json& source, std::string_view field)
{
  const auto found = source.find(std::string(field));
  if (found != source.end() && found->is_string())
    destination[std::string(field)] = *found;
}

bool terminal_tls_authentication_failure(std::string_view reason)
{
  return reason == "certificate_unknown" || reason == "unknown_certificate_authority" ||
         reason == "bad_certificate";
}

std::uint32_t default_video_bitrate(std::uint32_t frames_per_second)
{
  return 600U + frames_per_second * 80U;
}

VideoTrackPreferences read_video_preferences(const json& body, std::string_view track)
{
  const auto value = body.value(std::string(track), json::object());
  const auto requested_fps = value.value("targetFps", 15);
  const auto target_fps = static_cast<std::uint32_t>(std::clamp(requested_fps, 1, 15));
  const auto requested_bitrate =
      value.value("maxBitrateKbps", static_cast<int>(default_video_bitrate(target_fps)));
  return {target_fps, static_cast<std::uint32_t>(std::clamp(requested_bitrate, 250, 5000))};
}

void record_websocket_boundary_failure(EnduranceEventLog* log,
                                       std::string_view event,
                                       std::string_view error) noexcept
{
  if (!log)
    return;
  try
  {
    log->record("websocket", event, {{"error", std::string(error).substr(0, 240)}});
  }
  catch (...)
  {
  }
}

} // namespace

GatewayHttpServer::GatewayHttpServer(HttpConfig config,
                                     std::vector<ProviderSession> provider_sessions,
                                     PairingManager& p,
                                     HotkeyService& hotkeys,
                                     EnduranceEventLog* endurance_log,
                                     teleprompter::IRepository* teleprompter,
                                     tally::PublicationService* remote_tallies)
    : config_(std::move(config)), provider_sessions_(std::move(provider_sessions)), pairing_(p),
      hotkeys_(hotkeys), endurance_log_(endurance_log), teleprompter_(teleprompter),
      remote_tallies_(remote_tallies),
      client_events_([this](std::uint64_t id) { countdown_events_.disconnect(id); },
                     [this](Clock::time_point now) { countdown_events_.expire(now); })
{
  if (provider_sessions_.empty())
    throw std::invalid_argument("At least one provider session is required");
  std::unordered_set<std::string> provider_ids;
  for (auto& session : provider_sessions_)
  {
    if (!session.runtime || !session.webrtc || session.id.empty())
      throw std::invalid_argument("Provider sessions must be complete");
    if (!provider_ids.insert(session.id).second)
      throw std::invalid_argument("Provider session routing must be unambiguous");
    session.event_scope_id = new_client_event_id();
  }
  client_event_protocols_.add("countdownEvent",
                              [this](const json& envelope)
                              { return countdown_notification(envelope, countdown_events_); });
  callbacks_.log_message = &GatewayHttpServer::handle_civet_log;
  callbacks_.init_ssl = &GatewayHttpServer::initialize_civet_tls;
  callbacks_.connection_close = [](const mg_connection* connection) noexcept
  {
    auto* self = static_cast<GatewayHttpServer*>(
        const_cast<void*>(civet_user_context(mg_get_context(connection))));
    if (!self)
      return;
    try
    {
      std::lock_guard lock(self->sockets_mutex_);
      auto* socket = const_cast<mg_connection*>(connection);
      if (!self->sockets_.contains(socket))
        self->socket_devices_.erase(socket);
    }
    catch (...)
    {
      record_websocket_boundary_failure(self->endurance_log_, "connectionCleanupFailed", "unknown");
    }
  };
  callbacks_.connection_closed = [](const mg_connection* connection) noexcept
  {
    auto* self = static_cast<GatewayHttpServer*>(
        const_cast<void*>(civet_user_context(mg_get_context(connection))));
    if (!self)
      return;
    try
    {
      // Failed handshakes skip the WebSocket close callback. Retire slots only after native TLS
      // and socket cleanup; established event writers already drained in handleClose. No I/O here.
      std::unique_ptr<RequestAdmission::RequestReservation> reservation;
      std::lock_guard lock(self->sockets_mutex_);
      auto* socket = const_cast<mg_connection*>(connection);
      if (const auto found = self->websocket_reservations_.find(socket);
          found != self->websocket_reservations_.end())
      {
        reservation = std::move(found->second);
        self->websocket_reservations_.erase(found);
      }
    }
    catch (const std::exception& error)
    {
      record_websocket_boundary_failure(
          self->endurance_log_, "connectionCleanupFailed", error.what());
    }
    catch (...)
    {
      record_websocket_boundary_failure(self->endurance_log_, "connectionCleanupFailed", "unknown");
    }
  };
}
GatewayHttpServer::~GatewayHttpServer()
{
  stop();
}
void GatewayHttpServer::configure(HttpConfig config)
{
  if (server_ || maintenance_thread_.joinable())
    throw std::logic_error("The HTTP server cannot be reconfigured while it is running");
  config_ = std::move(config);
}
bool GatewayHttpServer::running() const noexcept
{
  return server_ != nullptr;
}
void GatewayHttpServer::start()
{
  if (server_ || maintenance_thread_.joinable())
    throw std::logic_error("HTTPS server must be stopped before starting again");
  state_callback_lifetime_->open();
  std::vector<std::string> options = {"listening_ports",
                                      config_.listening_ports.empty()
                                          ? std::to_string(config_.port) + "s"
                                          : config_.listening_ports,
                                      "ssl_protocol_version",
                                      "4",
                                      "ssl_verify_peer",
                                      "no",
                                      "error_log_file",
                                      config_.error_log.string(),
                                      "num_threads",
                                      std::to_string(server_worker_threads),
                                      "connection_queue",
                                      "64",
                                      "request_timeout_ms",
                                      "10000",
                                      // Keep control frames visible to WebSocketFrameRouter so it
                                      // can record diagnostics and echo Ping payloads explicitly.
                                      "enable_websocket_ping_pong",
                                      "no",
                                      "enable_auth_domain_check",
                                      "no"};
  if (!config_.initialize_tls_context)
  {
    options.emplace_back("ssl_certificate");
    options.push_back(config_.certificate.string());
  }
  tls_context_ = std::make_unique<HttpTlsContext>(config_.initialize_tls_context);
  try
  {
    server_ = std::make_unique<CivetServer>(options, &callbacks_, this);
  }
  catch (...)
  {
    if (tls_context_->failed())
      throw std::runtime_error("Could not initialize HTTPS server identity");
    throw;
  }
  server_->addHandler("/v1", this);
  server_->addWebSocketHandler("/v1/ws", this);
  delivery_stopping_ = false;
  maintenance_thread_ = std::thread([this] { request_cleanup_loop(); });
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record("http",
                           "serverStarted",
                           {{"workerThreads", server_worker_threads},
                            {"connectionQueue", 64},
                            {"clientCertificateRequired", false}});
  std::size_t provider_index = 0;
  for (auto& session : provider_sessions_)
  {
    if (session.telestrator)
      session.telestrator->on_changed(
          [this, lifetime = state_callback_lifetime_, provider = session.id]
          {
            const auto lease = lifetime->enter();
            if (lease)
              publish_capabilities(provider);
          });
    // Keep one guarded StateStore callback per provider across listener replacement. The
    // registration count also makes retry after a partially failed start idempotent.
    if (provider_index++ >= registered_state_subscriptions_)
    {
      session.runtime->state().subscribe(
          [this, lifetime = state_callback_lifetime_, provider = session.id](uint64_t,
                                                                             const json& changed)
          {
            const auto lease = lifetime->enter();
            if (!lease)
              return;
            const auto scope = state_publication_scope(changed);
            if (scope == StatePublicationScope::buses_only)
            {
              publish_bus_replacements(provider);
              return;
            }
            if (scope == StatePublicationScope::outputs_only)
            {
              publish_output_replacements(provider);
              return;
            }
            if (scope == StatePublicationScope::audio_inputs_only)
            {
              for (const auto& device : connected_devices())
                if (device.production_provider == provider)
                  queue_message(audio_input_replacement(device).dump(), device.id);
              return;
            }
            if (scope == StatePublicationScope::composition_layers_only)
            {
              const auto composition_id = state_publication_composition_id(changed);
              if (!composition_id)
                return;
              const bool metadata_changed =
                  changed.contains("compositionMetadata." + *composition_id);
              for (const auto& device : connected_devices())
                if (device.production_provider == provider)
                {
                  if (metadata_changed)
                    queue_message(composition_replacement(device).dump(), device.id);
                  queue_message(layer_replacement(device, *composition_id).dump(), device.id);
                }
              return;
            }
            if (scope == StatePublicationScope::media_state_only)
            {
              for (const auto& device : connected_devices())
                if (device.production_provider == provider)
                  queue_message(state_replacement(device).dump(), device.id);
              return;
            }
            if (scope == StatePublicationScope::streaming_destinations_only)
            {
              for (const auto& device : connected_devices())
                if (device.production_provider == provider)
                  if (const auto replacement = streaming_destination_replacement(device))
                    queue_message(replacement->dump(), device.id);
              return;
            }
            ++capability_revision_;
            for (const auto& device : connected_devices())
              if (device.production_provider == provider)
                publish_replacements(device);
          });
      ++registered_state_subscriptions_;
    }
    session.runtime->on_message(
        [this, provider = session.id, authority = session.event_authority](const json& message)
        {
          const auto type = message.value("type", "");
          if (type == "criticalControlsBlocked")
            authority->block(true);
          else if (type == "criticalControlsUnblocked")
            authority->block(false);
          if (message.contains("requestId") &&
              (type == "commandCompleted" || type == "commandFailed"))
          {
            auto response = message;
            const auto device_id = response.value("_deviceId", "");
            response.erase("_deviceId");
            response["productionProvider"] = provider;
            response["contractRevision"] = 2;
            if (type == "commandCompleted")
            {
              response["resultingStateSequence"] = response.value("resultingRevision", 0ULL);
              response.erase("resultingRevision");
            }
            queue_message(response.dump(), device_id, provider, ClientDeliveryPriority::control);
          }
          else if (type == "commandCompleted")
          {
            queue_message(json{{"type", "commandCompleted"},
                               {"commandId", message.value("commandId", "")},
                               {"operationId", message.value("operationId", "")},
                               {"resultingStateSequence", message.value("resultingRevision", 0ULL)}}
                              .dump(),
                          std::nullopt,
                          provider,
                          ClientDeliveryPriority::control);
          }
          else if (type == "commandFailed")
          {
            Device provider_device;
            provider_device.production_provider = provider;
            queue_message(
                asynchronous_command_rejection(
                    message, provider_session(provider_device).runtime->state().revision())
                    .dump(),
                std::nullopt,
                provider,
                ClientDeliveryPriority::control);
          }
          else if (type.starts_with("critical"))
          {
            auto public_message = message;
            std::optional<std::string> device_id;
            if (const auto found = public_message.find("_deviceId");
                found != public_message.end() && found->is_string())
            {
              device_id = found->get<std::string>();
              public_message.erase(found);
            }
            queue_message(public_message.dump(),
                          std::move(device_id),
                          provider,
                          ClientDeliveryPriority::control);
          }
        });
  }
}
void GatewayHttpServer::stop()
{
  state_callback_lifetime_->close();
  if (endurance_log_ && server_)
    endurance_log_->record("http", "serverStopping", diagnostics());
  for (auto& session : provider_sessions_)
    session.runtime->on_message({});
  delivery_stopping_ = true;
  maintenance_cv_.notify_all();
  if (maintenance_thread_.joinable())
    maintenance_thread_.join();
  const auto connected_at_shutdown = connected_devices();
  {
    std::lock_guard lock(sockets_mutex_);
    sockets_.clear();
    socket_devices_.clear();
  }
  for (const auto& device : connected_at_shutdown)
  {
    try
    {
      provider_session(device).runtime->audit().mark_device_seen(device.id);
    }
    catch (const std::exception& error)
    {
      if (endurance_log_ && endurance_log_->enabled())
        endurance_log_->record(
            "websocket",
            "shutdownActivityFailed",
            {{"deviceId", device.id}, {"error", std::string(error.what()).substr(0, 240)}});
    }
  }
  server_.reset();
  // Native close callbacks have finished; also release any incomplete-start bookkeeping.
  std::lock_guard lock(sockets_mutex_);
  websocket_reservations_.clear();
}
std::string GatewayHttpServer::uri(const mg_connection* connection) const
{
  auto info = mg_get_request_info(connection);
  return info && info->local_uri ? info->local_uri : "";
}
json GatewayHttpServer::read_json(mg_connection* connection) const
{
  auto info = mg_get_request_info(connection);
  long long length = info ? info->content_length : 0;
  if (length < 0 || static_cast<std::uint64_t>(length) > maximum_client_json_bytes)
    throw std::runtime_error("invalid body length");
  std::string body(static_cast<size_t>(length), '\0');
  size_t offset = 0;
  while (offset < body.size())
  {
    int count = mg_read(connection, body.data() + offset, body.size() - offset);
    if (count <= 0)
      throw std::runtime_error("incomplete request body");
    offset += count;
  }
  return body.empty() ? json::object() : parse_client_json_object(body);
}

ProviderSession& GatewayHttpServer::provider_session(const Device& device) const
{
  for (const auto& session : provider_sessions_)
    if (session.id == device.production_provider)
      return const_cast<ProviderSession&>(session);
  throw std::invalid_argument("The paired production provider is not installed");
}

json GatewayHttpServer::backend_status(const Device& device) const
{
  const auto& session = provider_session(device);
  const bool online = session.runtime->state().fresh();
  return {{"type", "backendStatus"},
          {"productionProvider", session.id},
          {"status", online ? "online" : "offline"},
          {"operational", online},
          {"reason", online ? "" : "production-backend-unavailable"}};
}

json GatewayHttpServer::capabilities(const Device& device) const
{
  return capabilities(device, provider_session(device).runtime->state().production_catalogs());
}

json GatewayHttpServer::capabilities(const Device& device,
                                     const ProductionCatalogSnapshot& catalogs) const
{
  refresh_event_authority(device);
  auto& session = provider_session(device);
  if (session.runtime->obs().backend_info().kind == ProductionBackendKind::companion_teleprompter)
  {
    auto feature = teleprompter_session_capability();
    const bool available =
        teleprompter_ && teleprompter_->shared_sessions() && session.runtime->state().fresh();
    feature["availability"] = available ? "available" : "temporarily-unavailable";
    if (!available)
      feature["reason"] = "teleprompter-unavailable";
    feature["scriptsEndpoint"] = "/v1/teleprompter/scripts";
    feature["progressReports"] = true;
    feature["maxContentBytes"] = teleprompter::maximum_content_bytes;
    return {{"contractRevision", 2},
            {"capabilityRevision", capability_revision_.load()},
            {"features", {{"companion-teleprompter", std::move(feature)}}}};
  }
  auto value =
      session.runtime->state().generic_capabilities(device.role,
                                                    capability_revision_.load(),
                                                    session.runtime->thumbnails_available(),
                                                    session.webrtc->audio_monitor_status(),
                                                    session.webrtc->audio_master_status());
  if (device.production_provider == "obs" && remote_tallies_)
  {
    const auto availability = remote_tallies_->availability(device.id);
    value["features"]["remoteTally.publish"] = {
        {"version", 1},
        {"access", "observe"},
        {"available", availability.available},
        {"availability", availability.available ? "available" : "temporarily-unavailable"},
        {"reason", availability.available ? json(nullptr) : json(availability.reason)},
        {"listEndpoint", "/v1/remote-tallies"}};
  }
  if (teleprompter_)
    value["features"]["view.teleprompter.scripts"] = {
        {"access", "observe"},
        {"availability", "available"},
        {"version", 1},
        {"listEndpoint", "/v1/teleprompter/scripts"},
        {"progressReports", true},
        {"limits",
         {{"maxScripts", teleprompter::maximum_scripts_per_device},
          {"maxContentBytes", teleprompter::maximum_content_bytes}}}};
  if (teleprompter_ && teleprompter_->shared_sessions() &&
      (device.production_provider == "obs" || device.production_provider == "vmix" ||
       device.production_provider == "streamlabs"))
    value["features"]["view.teleprompter.shared"] = teleprompter_session_capability();
  if (device.role >= Role::operator_ && session.runtime->critical_controls_available())
  {
    auto backend_capabilities = session.runtime->obs().backend_info().capabilities;
    backend_capabilities.recording_pause =
        backend_capabilities.recording_pause && session.runtime->obs().recording_pause_available();
    const auto actions = critical_action_capability_names(backend_capabilities);
    if (!actions.empty())
      value["features"]["criticalLiveControlFifo"] = {{"available", true},
                                                      {"version", 1},
                                                      {"transitionSelection", "transitionId"},
                                                      {"maxInFlight", 16},
                                                      {"staleAfterMs", 1500},
                                                      {"actions", actions}};
  }
  if (device.role >= Role::operator_ &&
      session.runtime->obs().backend_info().capabilities.hotkey_trigger)
  {
    const bool production_available = hotkeys_.available(device, HotkeyTarget::production);
    const bool system_available = hotkeys_.available(device, HotkeyTarget::system);
    json capability = {
        {"access", "control"},
        {"availability", production_available ? "available" : "temporarily-unavailable"},
        {"defaultTarget", "production"},
        {"targets",
         {{"production",
           {{"availability", production_available ? "available" : "temporarily-unavailable"}}},
          {"system",
           {{"availability", system_available ? "available" : "temporarily-unavailable"}}}}},
        {"limits", {{"type", "hotkey-chord"}, {"maximumKeys", 8}}}};
    if (!production_available)
      capability["reason"] = "production-backend-unavailable";
    value["features"]["system.hotkey.trigger"] = std::move(capability);
    if (session.runtime->obs().hotkey_catalog_supported())
    {
      value["features"]["production.hotkey.catalog"] = {
          {"access", "observe"},
          {"availability", production_available ? "available" : "temporarily-unavailable"}};
    }
  }
  if (device.role >= Role::operator_ && device.production_provider == "obs" &&
      session.runtime->obs().backend_info().id == "obs")
  {
    const bool available = session.runtime->obs().connected();
    const auto availability = available ? "available" : "temporarily-unavailable";
    json profile_capability = {{"access", "control"},
                               {"availability", availability},
                               {"listEndpoint", "/v1/obs/profiles"},
                               {"selectionEndpoint", "/v1/obs/profiles/current"}};
    json collection_capability = {{"access", "control"},
                                  {"availability", availability},
                                  {"listEndpoint", "/v1/obs/scene-collections"},
                                  {"selectionEndpoint", "/v1/obs/scene-collections/current"}};
    if (!available)
    {
      profile_capability["reason"] = "production-backend-unavailable";
      collection_capability["reason"] = "production-backend-unavailable";
    }
    value["features"]["production.obs.profiles"] = std::move(profile_capability);
    value["features"]["production.obs.sceneCollections"] = std::move(collection_capability);
  }
  if (!session.runtime->obs().backend_info().capabilities.system_hotkeys &&
      session.runtime->obs().hotkey_catalog_supported())
    value["features"]["production.hotkey.catalog"] = {
        {"access", "observe"},
        {"availability",
         session.runtime->obs().connected() ? "available" : "temporarily-unavailable"}};
  if (catalogs.canvas)
    value["features"]["composition.canvas.observe"] = {
        {"access", "observe"},
        {"availability", "available"},
        {"limits", {{"type", "canvas"}, {"endpoint", "/v1/canvases"}}}};
  const auto provider_capabilities = session.runtime->obs().backend_info().capabilities;
  if (!provider_capabilities.program_observation)
    value["features"].erase("composition.program.observe");
  if (!provider_capabilities.system_hotkeys && value["features"].contains("system.hotkey.trigger"))
  {
    auto& feature = value["features"]["system.hotkey.trigger"];
    feature["targets"].erase("system");
    feature["limits"]["systemKeysSupported"] = false;
    feature["limits"]["productionKeysOnly"] = true;
  }
  if (catalogs.collections)
    value["features"]["production.sceneCollections"] = {
        {"access", device.role >= Role::operator_ ? "control" : "observe"},
        {"availability",
         session.runtime->obs().connected() ? "available" : "temporarily-unavailable"},
        {"limits",
         {{"listEndpoint", "/v1/scene-collections"},
          {"selectionEndpoint", "/v1/scene-collections/current"}}}};
  add_telestrator_capability(value["features"], device, session.telestrator);
  return value;
}

json GatewayHttpServer::state_replacement(const Device& device) const
{
  auto value = provider_session(device).runtime->state().generic_state(device.role);
  value["productionProvider"] = device.production_provider;
  add_canvas_state(value, provider_session(device).runtime->state().production_catalogs().canvas);
  return {{"type", "stateReplacement"}, {"state", std::move(value)}};
}

json GatewayHttpServer::composition_replacement(const Device& device) const
{
  auto& runtime = *provider_session(device).runtime;
  auto value = runtime.state().generic_compositions(runtime.thumbnails_available());
  return {{"type", "compositionReplacement"},
          {"stateSequence", value["stateSequence"]},
          {"compositions", value["compositions"]}};
}

json GatewayHttpServer::transition_replacement(const Device& device) const
{
  auto value = provider_session(device).runtime->state().generic_transitions();
  return transition_replacement_payload(value);
}

std::optional<json> GatewayHttpServer::streaming_destination_replacement(const Device& device) const
{
  auto& runtime = *provider_session(device).runtime;
  if (!runtime.state().streaming_destinations_available())
    return std::nullopt;
  auto value = runtime.state().generic_streaming_destinations();
  value["productionProvider"] = device.production_provider;
  return json{{"type", "streamingDestinationReplacement"},
              {"stateSequence", value["stateSequence"]},
              {"productionProvider", device.production_provider},
              {"aggregate", value["aggregate"]},
              {"destinations", value["destinations"]}};
}

std::vector<json> GatewayHttpServer::layer_replacements(const Device& device) const
{
  std::vector<json> result;
  auto& runtime = *provider_session(device).runtime;
  const auto compositions = runtime.state().generic_compositions(runtime.thumbnails_available());
  for (const auto& composition : compositions["compositions"])
    result.push_back(layer_replacement(device, composition.value("id", "")));
  return result;
}

json GatewayHttpServer::layer_replacement(const Device& device,
                                          std::string_view composition_id) const
{
  auto& runtime = *provider_session(device).runtime;
  const auto layers =
      runtime.state().generic_layers(composition_id, runtime.thumbnails_available());
  return {{"type", "layerReplacement"},
          {"stateSequence", layers["stateSequence"]},
          {"compositionId", layers["compositionId"]},
          {"sourceMembership", layers["sourceMembership"]},
          {"layers", layers["layers"]}};
}

json GatewayHttpServer::audio_input_replacement(const Device& device) const
{
  const auto inputs = provider_session(device).runtime->state().generic_audio_inputs();
  json replacement = {{"type", "audioInputReplacement"},
                      {"stateSequence", inputs["stateSequence"]},
                      {"auditionBusActivationEnabled", inputs["auditionBusActivationEnabled"]},
                      {"audioInputs", inputs["audioInputs"]}};
  if (const auto master = inputs.find("master"); master != inputs.end())
    replacement["master"] = *master;
  return replacement;
}

void GatewayHttpServer::publish_replacements(const Device& device)
{
  const auto catalogs = provider_session(device).runtime->state().production_catalogs();
  const auto current = capabilities(device, catalogs);
  for (const auto& replacement : production_catalog_replacements(catalogs))
    queue_message(replacement.dump(), device.id);
  queue_message(json{{"type", "capabilityReplacement"},
                     {"contractRevision", 2},
                     {"capabilityRevision", current["capabilityRevision"]},
                     {"features", current["features"]}}
                    .dump(),
                device.id);
  queue_message(state_replacement(device).dump(), device.id);
  queue_message(backend_status(device).dump(), device.id);
  queue_message(composition_replacement(device).dump(), device.id);
  queue_message(transition_replacement(device).dump(), device.id);
  if (const auto replacement = streaming_destination_replacement(device))
    queue_message(replacement->dump(), device.id);
  for (const auto& layers : layer_replacements(device))
    queue_message(layers.dump(), device.id);
  queue_message(audio_input_replacement(device).dump(), device.id);
}

void GatewayHttpServer::publish_replacements()
{
  ++capability_revision_;
  for (const auto& device : connected_devices())
    publish_replacements(device);
}

void GatewayHttpServer::publish_capabilities(std::string_view production_provider)
{
  ++capability_revision_;
  for (const auto& device : connected_devices())
  {
    if (device.production_provider != production_provider)
      continue;
    const auto catalogs = provider_session(device).runtime->state().production_catalogs();
    for (const auto& replacement : production_catalog_replacements(catalogs))
      queue_message(replacement.dump(), device.id);
    const auto current = capabilities(device, catalogs);
    queue_message(json{{"type", "capabilityReplacement"},
                       {"contractRevision", 2},
                       {"capabilityRevision", current["capabilityRevision"]},
                       {"features", current["features"]}}
                      .dump(),
                  device.id);
  }
}

GatewayHttpServer::GenericCommandOutcome
GatewayHttpServer::execute_generic_command(const Device& device, const json& envelope)
{
  auto& runtime = *provider_session(device).runtime;
  const auto id_field = envelope.find("commandId");
  if (id_field == envelope.end() || !id_field->is_string() ||
      id_field->get_ref<const std::string&>().empty() ||
      id_field->get_ref<const std::string&>().size() > 128)
    return {422,
            {{"status", "rejected"},
             {"reason", "invalid-command-state"},
             {"currentStateSequence", runtime.state().revision()}}};
  const auto& command_id = id_field->get_ref<const std::string&>();
  if (runtime.obs().backend_info().kind == ProductionBackendKind::companion_teleprompter)
    return {409,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "capability-unavailable"},
             {"currentStateSequence", runtime.state().revision()}}};
  const auto cache_key = device.id + ":" + command_id;
  expire_client_requests(Clock::now());
  GenericCommandAdmission admission{*this, cache_key};
  {
    std::lock_guard lock(generic_command_cache_mutex_);
    if (const auto found = generic_command_cache_.find(cache_key);
        found != generic_command_cache_.end())
      return found->second.second;
    // Reserve before any backend/configuration effect. Live receipts are never evicted to make
    // room. Stored outcomes have a fixed schema: bounded command ID, generated operation ID,
    // literal status/reason and a numeric sequence (less than 8 KiB including the cache key).
    if (cache_key.size() > 512 ||
        generic_command_cache_.size() + generic_commands_in_flight_.size() >= 16'384 ||
        !generic_commands_in_flight_.insert(cache_key).second)
      return {429,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "command-capacity-exceeded"}}};
    admission.admitted = true;
  }

  const auto current_sequence = runtime.state().revision();
  const auto command = envelope.find("command");
  if (command == envelope.end() || !command->is_object() ||
      (command->contains("type") && !command->at("type").is_string()))
    return {422,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "invalid-command-state"},
             {"currentStateSequence", current_sequence}}};

  const auto type = command->value("type", "");
  if (type == "execute-transition" || type == "change-recording" || type == "change-streaming")
  {
    const auto family = type == "execute-transition" ? "transition triggers"
                        : type == "change-recording" ? "recording controls"
                                                     : "streaming controls";
    return {409,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "critical-control-required"},
             {"message", "Use POST /v1/critical-actions for " + std::string(family) + "."}}};
  }
  if ((type == "set-input-volume" || type == "set-input-balance" || type == "set-master-volume" ||
       type == "set-input-monitoring" || type == "set-audio-mute") &&
      device.role < Role::operator_)
    return {403,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "permission-required"},
             {"currentStateSequence", current_sequence}}};
  if (type == "set-audio-source-audition-bus" && device.role < Role::admin)
    return {403,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "permission-required"},
             {"currentStateSequence", current_sequence}}};
  const auto sequence_validation = validate_expected_state_sequence(envelope, current_sequence);
  if (sequence_validation != StateSequenceValidation::accepted)
    return {409,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "resource-stale"},
             {"currentStateSequence", current_sequence}}};
  if (type == "set-input-monitoring" && !runtime.state().audio_input_monitoring_available())
    return {503,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "backend-unavailable"},
             {"currentStateSequence", current_sequence}}};

  std::string action;
  json body = {{"commandId", command_id}};
  const auto has_non_string_field = [&](std::initializer_list<std::string_view> fields)
  {
    return std::any_of(fields.begin(),
                       fields.end(),
                       [&](std::string_view field)
                       {
                         const auto value = command->find(std::string(field));
                         // Missing fields retain their existing defaults and semantic validation
                         // below.
                         return value != command->end() && !value->is_string();
                       });
  };
  const auto invalid_command = [&]() -> GenericCommandOutcome
  {
    return {422,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "invalid-command-state"},
             {"currentStateSequence", current_sequence}}};
  };
  if (type == "select-composition")
  {
    if (has_non_string_field({"compositionId", "target"}))
      return invalid_command();
    action = "select-scene";
    body["sceneId"] = command->value("compositionId", "");
    body["target"] = command->value("target", "");
  }
  else if (type == "set-layer-visibility")
  {
    if (has_non_string_field({"compositionId", "layerId"}))
      return invalid_command();
    const auto composition_id = command->value("compositionId", "");
    const auto layer_id = command->value("layerId", "");
    const auto target = runtime.state().layer_command_target(composition_id, layer_id);
    if (!target)
      return {404,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "resource-stale"},
               {"currentStateSequence", current_sequence}}};
    if (!target->visibility_controllable)
      return {422,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "visibility-not-controllable"},
               {"currentStateSequence", current_sequence}}};
    if (!target->visibility_state_available)
      return {503,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "backend-unavailable"},
               {"currentStateSequence", current_sequence}}};
    action = "set-scene-item-visibility";
    body["sceneId"] = composition_id;
    body["sceneItemId"] = target->scene_item_id;
    body["enabled"] = command->value("visible", json());
    body["_expectedResourceId"] = target->resource_id;
  }
  else if (type == "set-audio-mute")
  {
    if (has_non_string_field({"trackId"}))
      return invalid_command();
    action = "set-audio-mute";
    body["trackId"] = command->value("trackId", "");
    body["muted"] = command->value("muted", json());
  }
  else if (type == "set-input-mute")
  {
    if (has_non_string_field({"inputId"}))
      return invalid_command();
    action = "set-input-mute";
    body["inputId"] = command->value("inputId", "");
    body["muted"] = command->value("muted", json());
  }
  else if (type == "set-input-solo")
  {
    if (has_non_string_field({"inputId"}))
      return invalid_command();
    action = "set-input-solo";
    body["inputId"] = command->value("inputId", "");
    body["solo"] = command->value("solo", json());
  }
  else if (type == "set-input-volume")
  {
    if (has_non_string_field({"inputId"}))
      return invalid_command();
    action = "set-input-volume";
    body["inputId"] = command->value("inputId", "");
    body["volumeDb"] = command->value("volumeDb", json());
  }
  else if (type == "set-input-balance")
  {
    if (has_non_string_field({"inputId"}))
      return invalid_command();
    action = "set-input-balance";
    body["inputId"] = command->value("inputId", "");
    body["balance"] = command->value("balance", json());
  }
  else if (type == "set-master-volume")
  {
    action = "set-master-volume";
    body["volumeDb"] = command->value("volumeDb", json());
  }
  else if (type == "set-input-monitoring")
  {
    if (has_non_string_field({"inputId"}))
      return invalid_command();
    action = "set-input-monitoring";
    body["inputId"] = command->value("inputId", "");
    body["monitoringMode"] = command->value("monitoringMode", json());
  }
  else if (type == "set-audio-source-audition-bus")
  {
    const auto enabled = command->find("enabled");
    if (enabled == command->end() || !enabled->is_boolean())
      return {422,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "invalid-command-state"},
               {"currentStateSequence", current_sequence}}};

    GlobalSettingChangeRequestHandler handler;
    {
      std::lock_guard lock(global_setting_change_request_handler_mutex_);
      handler = global_setting_change_request_handler_;
    }
    if (!handler)
      return {503,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "backend-unavailable"},
               {"currentStateSequence", current_sequence}}};

    const auto operation_id = std::to_string(monotonic_ns());
    GenericCommandOutcome outcome{
        202, {{"status", "accepted"}, {"commandId", command_id}, {"operationId", operation_id}}};
    {
      std::lock_guard lock(generic_command_cache_mutex_);
      generic_command_cache_.insert_or_assign(cache_key, std::pair{Clock::now(), outcome});
      global_setting_request_operations_.insert_or_assign(
          cache_key, PendingGlobalSettingRequest{operation_id});
    }
    RoleChangeRequestDisposition disposition = RoleChangeRequestDisposition::unavailable;
    try
    {
      disposition = handler(device, command_id, "audio-source-audition-bus", *enabled);
    }
    catch (const std::exception&)
    {
      runtime.audit().command(
          device.id, type, "audio-source-audition-bus", "approval_handler_failed", operation_id);
    }
    if (disposition == RoleChangeRequestDisposition::unavailable)
    {
      GenericCommandOutcome rejected{503,
                                     {{"status", "rejected"},
                                      {"commandId", command_id},
                                      {"reason", "permission-required"},
                                      {"currentStateSequence", current_sequence}}};
      std::lock_guard lock(generic_command_cache_mutex_);
      generic_command_cache_.insert_or_assign(cache_key, std::pair{Clock::now(), rejected});
      global_setting_request_operations_.erase(cache_key);
      return rejected;
    }
    if (disposition == RoleChangeRequestDisposition::approved)
      complete_global_setting_change_request(device.id, command_id, true);
    return outcome;
  }
  else if (type == "request-access")
  {
    if (has_non_string_field({"level"}))
      return invalid_command();
    const auto level = command->value("level", "");
    if (level != "viewer" && level != "operator")
      return {422,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "invalid-command-state"},
               {"currentStateSequence", current_sequence}}};
    const auto requested_role = parse_role(level);
    return execute_role_request(device, command_id, requested_role, current_sequence);
  }
  else if (type == "rename-client")
  {
    const auto name_field = command->find("name");
    if (name_field == command->end() || !name_field->is_string() ||
        !valid_device_name(name_field->get_ref<const std::string&>()))
      return {422,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "invalid-command-state"},
               {"currentStateSequence", current_sequence}}};
    const auto& name = name_field->get_ref<const std::string&>();
    if (!runtime.audit().rename_device(device.id, name))
      return {403,
              {{"status", "rejected"},
               {"commandId", command_id},
               {"reason", "permission-required"},
               {"currentStateSequence", current_sequence}}};
    {
      std::lock_guard lock(sockets_mutex_);
      for (auto& [connection, connected_device] : socket_devices_)
      {
        static_cast<void>(connection);
        if (connected_device.id == device.id)
          connected_device.name = name;
      }
    }
    const auto operation_id = std::to_string(monotonic_ns());
    GenericCommandOutcome outcome{
        202, {{"status", "accepted"}, {"commandId", command_id}, {"operationId", operation_id}}};
    {
      std::lock_guard lock(generic_command_cache_mutex_);
      generic_command_cache_.insert_or_assign(cache_key, std::pair{Clock::now(), outcome});
    }
    queue_message(json{{"type", "commandCompleted"},
                       {"commandId", command_id},
                       {"operationId", operation_id},
                       {"resultingStateSequence", current_sequence}}
                      .dump(),
                  device.id,
                  std::nullopt,
                  ClientDeliveryPriority::control);
    return outcome;
  }

  if (action.empty())
    return {422,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason", "invalid-command-state"},
             {"currentStateSequence", current_sequence}}};

  const auto result = runtime.execute(device, action, body);
  if (result.value("status", "") != "accepted")
  {
    const auto reason = result.value("reason", "");
    const auto reason_code = result.value("reasonCode", "");
    const bool permission = reason == "operator role required";
    const bool unavailable = reason == "OBS state is stale" || reason_code == "backend-unavailable";
    const bool missing =
        reason_code == "resource-stale" || reason.find("not found") != std::string::npos;
    const bool conflict = result.value("status", "") == "conflict";
    const bool capacity = reason_code == "command-capacity-exceeded";
    const int status = capacity      ? 429
                       : permission  ? 403
                       : unavailable ? 503
                       : missing     ? 404
                       : conflict    ? 409
                                     : 422;
    return {status,
            {{"status", "rejected"},
             {"commandId", command_id},
             {"reason",
              capacity                                 ? "command-capacity-exceeded"
              : permission                             ? "permission-required"
              : unavailable                            ? "backend-unavailable"
              : missing                                ? "resource-stale"
              : reason_code == "audio-track-read-only" ? "audio-track-read-only"
                                                       : "invalid-command-state"},
             {"currentStateSequence", runtime.state().revision()}}};
  }

  GenericCommandOutcome outcome{202,
                                {{"status", "accepted"},
                                 {"commandId", command_id},
                                 {"operationId", result.value("operationId", "")}}};
  std::lock_guard lock(generic_command_cache_mutex_);
  generic_command_cache_.insert_or_assign(cache_key, std::pair{Clock::now(), outcome});
  return outcome;
}
void GatewayHttpServer::reply(mg_connection* c,
                              int status,
                              const json& value,
                              std::uint32_t retry_after_ms) const
{
  http_responses_.fetch_add(1, std::memory_order_relaxed);
  if (status >= 500)
    http_server_errors_.fetch_add(1, std::memory_order_relaxed);
  else if (status >= 400)
    http_client_errors_.fetch_add(1, std::memory_order_relaxed);
  if (endurance_log_ && endurance_log_->enabled())
  {
    const auto* info = mg_get_request_info(c);
    json details{{"method", info && info->request_method ? info->request_method : ""},
                 {"route", uri(c)},
                 {"status", status},
                 {"activeRequests", active_http_requests_.load(std::memory_order_relaxed)}};
    if (status >= 400)
    {
      add_string_field_if_present(details, value, "error");
      add_string_field_if_present(details, value, "reason");
      add_string_field_if_present(details, value, "reasonCode");
    }
    endurance_log_->record("http", "response", details);
  }
  auto body = value.dump();
  const auto retry_seconds = std::to_string((std::uint64_t(retry_after_ms) + 999) / 1000);
  const bool headers_sent =
      retry_after_ms
          ? send_response_headers(c,
                                  status,
                                  "application/json",
                                  body.size(),
                                  "no-store",
                                  {{"Retry-After", retry_seconds}})
          : send_response_headers(c, status, "application/json", body.size(), "no-store");
  if (!headers_sent || !send_response_body(c, body.data(), body.size()))
    http_server_errors_.fetch_add(1, std::memory_order_relaxed);
}

void GatewayHttpServer::reply_rate_limited(mg_connection* c,
                                           std::string_view scope,
                                           std::uint32_t retry_after_ms) const
{
  http_responses_.fetch_add(1, std::memory_order_relaxed);
  http_client_errors_.fetch_add(1, std::memory_order_relaxed);
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record("http",
                           "response",
                           {{"route", uri(c)},
                            {"status", 429},
                            {"scope", std::string(scope)},
                            {"retryAfterMs", retry_after_ms}});
  const auto retry_after_seconds = std::max(1U, (retry_after_ms + 999U) / 1'000U);
  const auto body = json{
      {"error", "rate_limited"},
      {"scope", std::string(scope)},
      {"retryAfterMs",
       retry_after_ms}}.dump();
  const auto retry_after_text = std::to_string(retry_after_seconds);
  if (!send_response_headers(c,
                             429,
                             "application/json",
                             body.size(),
                             "no-store",
                             {{"Retry-After", retry_after_text}}) ||
      !send_response_body(c, body.data(), body.size()))
    http_server_errors_.fetch_add(1, std::memory_order_relaxed);
}
void GatewayHttpServer::reply_thumbnail(mg_connection* c,
                                        const ThumbnailResult& result,
                                        std::string_view error) const
{
  if (result.status == ThumbnailResultStatus::not_modified)
  {
    http_responses_.fetch_add(1, std::memory_order_relaxed);
    if (endurance_log_ && endurance_log_->enabled())
      endurance_log_->record("thumbnail", "response", {{"route", uri(c)}, {"status", 304}});
    if (!send_response_headers(c, 304, {}, 0, "private, max-age=5", {{"ETag", result.etag}}))
      http_server_errors_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (result.status == ThumbnailResultStatus::image)
  {
    http_responses_.fetch_add(1, std::memory_order_relaxed);
    if (endurance_log_ && endurance_log_->enabled())
      endurance_log_->record("thumbnail",
                             "response",
                             {{"route", uri(c)},
                              {"status", 200},
                              {"bytes", result.bytes.size()},
                              {"stale", result.stale},
                              {"placeholder", result.placeholder}});
    const auto observed_at = std::to_string(result.observed_at_ns);
    if (!send_response_headers(
            c,
            200,
            result.content_type,
            result.bytes.size(),
            "private, max-age=5",
            {{"ETag", result.etag},
             {"X-Thumbnail-Observed-At-Monotonic-Ns", observed_at},
             {"X-Thumbnail-Stale", result.stale ? "true" : "false"},
             {"X-Thumbnail-Placeholder", result.placeholder ? "true" : "false"}}) ||
        !send_response_body(c, result.bytes.data(), result.bytes.size()))
      http_server_errors_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  int status = 503;
  if (result.status == ThumbnailResultStatus::resource_not_found)
    status = 404;
  else if (result.status == ThumbnailResultStatus::stale_state)
    status = 409;
  else if (result.status == ThumbnailResultStatus::queue_full)
  {
    reply_rate_limited(c, "thumbnails", 1'000);
    return;
  }
  reply(c, status, {{"error", std::string(error)}, {"reason", result.reason}});
}

void GatewayHttpServer::handle_thumbnail(mg_connection* c,
                                         const Device& device,
                                         std::string_view path,
                                         ThumbnailTarget target)
{
  auto& runtime = *provider_session(device).runtime;
  const auto generic_prefix = target == ThumbnailTarget::scene
                                  ? std::string_view("/v1/compositions/")
                                  : std::string_view("/v1/resources/");
  constexpr std::string_view suffix = "/thumbnail";
  const auto resource_id =
      path.substr(generic_prefix.size(), path.size() - generic_prefix.size() - suffix.size());
  const bool valid_id =
      !resource_id.empty() && resource_id.size() <= 128 &&
      std::all_of(resource_id.begin(),
                  resource_id.end(),
                  [](unsigned char value)
                  { return std::isalnum(value) || value == '-' || value == '_' || value == '.'; });
  const auto error = target == ThumbnailTarget::scene ? "composition_thumbnail_unavailable"
                                                      : "resource_thumbnail_unavailable";
  if (!valid_id)
  {
    reply(c,
          400,
          {{"error", error},
           {"reason",
            target == ThumbnailTarget::scene ? "malformed_composition_id"
                                             : "malformed_resource_id"}});
    return;
  }

  auto dimension = [&](const char* name, std::uint32_t fallback)
  {
    char value[32]{};
    const auto* info = mg_get_request_info(c);
    const int length =
        info && info->query_string
            ? mg_get_var(
                  info->query_string, std::strlen(info->query_string), name, value, sizeof(value))
            : -1;
    if (length == -1)
      return fallback;
    if (length < 0)
      throw std::invalid_argument("invalid thumbnail dimensions");
    std::uint32_t parsed = 0;
    const auto [end, parse_error] = std::from_chars(value, value + length, parsed);
    if (parse_error != std::errc{} || end != value + length || parsed < 32 || parsed > 320)
      throw std::invalid_argument("invalid thumbnail dimensions");
    return parsed;
  };

  try
  {
    const auto width = dimension("maxWidth", 192);
    const auto height = dimension("maxHeight", 192);
    const auto* etag = mg_get_header(c, "If-None-Match");
    const auto result =
        target == ThumbnailTarget::scene
            ? runtime.scene_thumbnail(resource_id, width, height, etag ? etag : "")
            : runtime.source_thumbnail(resource_id, width, height, etag ? etag : "");
    reply_thumbnail(c, result, error);
  }
  catch (const std::exception&)
  {
    reply(c, 400, {{"error", error}, {"reason", "invalid_dimensions"}});
  }
}
bool GatewayHttpServer::write_text(mg_connection* connection, std::string_view text)
{
  if (mg_websocket_write(connection, MG_WEBSOCKET_OPCODE_TEXT, text.data(), text.size()) > 0)
    return true;
  websocket_frames_.transport_error();
  return false;
}
int GatewayHttpServer::initialize_civet_tls(void* context, void* user_data) noexcept
{
  auto* self =
      static_cast<GatewayHttpServer*>(const_cast<void*>(civet_callback_user_context(user_data)));
  if (!self || !self->tls_context_)
    return -1;
  return self->tls_context_->initialize(context);
}

int GatewayHttpServer::handle_civet_log(const mg_connection* connection,
                                        const char* message) noexcept
{
  try
  {
    if (!connection || !message)
      return 0;
    const auto reason = authentication_diagnostics::tls_failure_reason(message);
    if (!reason)
      return 0;
    auto* self = static_cast<GatewayHttpServer*>(
        const_cast<void*>(civet_user_context(mg_get_context(connection))));
    if (self && self->endurance_log_ && self->endurance_log_->enabled())
    {
      const auto* request = mg_get_request_info(connection);
      const bool terminal = terminal_tls_authentication_failure(*reason);
      self->endurance_log_->record(
          "tls",
          "handshakeRejected",
          {{"reason", std::string(*reason)},
           {"remoteAddress", request ? std::string(request->remote_addr) : "unknown"},
           {"terminal", terminal},
           {"retryable", !terminal},
           {"requiredAction", terminal ? "repair_pairing" : "retry_with_backoff"}});
    }
  }
  catch (...)
  {
  }
  return 0;
}
RequestAdmission::Result GatewayHttpServer::permit_public(const mg_connection* connection,
                                                          RequestScope scope)
{
  const auto* info = mg_get_request_info(connection);
  return request_admission_.admit_public(info ? info->remote_addr : "unknown", scope, Clock::now());
}

RequestAdmission::Result GatewayHttpServer::permit_device(const Device& device, RequestScope scope)
{
  auto result =
      request_admission_.admit_device(device.production_provider, device.id, scope, Clock::now());
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record("http",
                           "admission",
                           {{"deviceId", device.id},
                            {"productionProvider", device.production_provider},
                            {"scope", request_scope_name(scope)},
                            {"admitted", result.retry_after_ms == 0},
                            {"retryAfterMs", result.retry_after_ms}});
  return result;
}
bool GatewayHttpServer::handleGet(CivetServer*, mg_connection* c)
{
  if (uri(c).starts_with("/v1/remote-tallies"))
    return handle_tally_request(c, HttpRequestMethod::get);
  http_requests_.fetch_add(1, std::memory_order_relaxed);
  const auto active = active_http_requests_.fetch_add(1, std::memory_order_relaxed) + 1;
  auto maximum = maximum_active_http_requests_.load(std::memory_order_relaxed);
  while (maximum < active && !maximum_active_http_requests_.compare_exchange_weak(
                                 maximum, active, std::memory_order_relaxed))
  {
  }
  struct RequestGuard
  {
    std::atomic<std::uint64_t>& count;
    ~RequestGuard()
    {
      count.fetch_sub(1, std::memory_order_relaxed);
    }
  } guard{active_http_requests_};
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record(
        "http", "requestStarted", {{"method", "GET"}, {"route", uri(c)}, {"active", active}});
  try
  {
    auto path = uri(c);
    if (path == "/v1/identity")
    {
      if (const auto admission = permit_public(c, RequestScope::identity); admission.retry_after_ms)
      {
        reply_rate_limited(c, "identity", admission.retry_after_ms);
        return true;
      }
      json production_providers = json::array();
      for (const auto& session : provider_sessions_)
      {
        Device provider_device;
        provider_device.production_provider = session.id;
        production_providers.push_back(backend_status(provider_device));
      }
      reply(c,
            200,
            {{"service", "panelive-companion"},
             {"apiVersion", 1},
             {"contractRevision", 2},
             {"instanceId", config_.identity.instance_id},
             {"name", config_.identity.name},
             {"certificateFingerprint", config_.identity.certificate_fingerprint},
             {"productionProviders", std::move(production_providers)}});
      return true;
    }
    auto authorization = authorize(c, "http");
    if (!authorization.device)
    {
      const auto admission = permit_public(c, RequestScope::authentication);
      if (admission.retry_after_ms)
        reply_rate_limited(c, "authentication", admission.retry_after_ms);
      else
        reply(c, 401, authentication_failure_response(authorization.failure_reason));
      return true;
    }
    const auto& device = *authorization.device;
    auto& session = provider_session(device);
    auto& runtime = *session.runtime;
    const auto scope = http_request_scope(HttpRequestMethod::get, path);
    const auto admission = permit_device(device, scope);
    if (admission.retry_after_ms)
    {
      reply_rate_limited(c, request_scope_name(scope), admission.retry_after_ms);
      return true;
    }
    if (path == "/v1/capabilities" && remote_tallies_ && device.production_provider == "obs")
      remote_tallies_->refresh_authorization(device.id);
    if (runtime.obs().backend_info().kind == ProductionBackendKind::companion_teleprompter)
    {
      const bool prompting = teleprompter_session_read_path(path) || teleprompter_read_path(path);
      if (prompting && !runtime.running())
      {
        reply(c, 503, {{"error", "teleprompter-unavailable"}});
        return true;
      }
      if (!prompting && path != "/v1/state" && path != "/v1/capabilities")
      {
        reply(c, 404, {{"error", "capability-unavailable"}});
        return true;
      }
    }
    if (teleprompter_session_read_path(path))
    {
      const auto response = teleprompter_session_read(
          teleprompter_ ? teleprompter_->shared_sessions() : nullptr, device.id, path);
      reply(c, response.status, response.body);
    }
    else if (teleprompter_read_path(path))
    {
      const auto response = teleprompter_read(teleprompter_, device.id, path);
      reply(c, response.status, response.body);
    }
    else if (path == "/v1/state")
    {
      auto value = runtime.state().generic_state(device.role);
      value["productionProvider"] = device.production_provider;
      value["backendStatus"] = backend_status(device);
      add_canvas_state(value, runtime.state().production_catalogs().canvas);
      reply(c, 200, value);
    }
    else if (path == "/v1/capabilities")
      reply(c, 200, capabilities(device));
    else if (path == "/v1/telestrator")
    {
      const auto outcome = telestrator_response(device, session.telestrator);
      reply(c, outcome.status, outcome.body);
    }
    else if (path == "/v1/performance")
    {
      if (!runtime.obs().backend_info().capabilities.performance)
      {
        reply(c, 404, {{"error", "production-performance-unsupported"}});
        return true;
      }
      const auto performance = runtime.state().generic_performance();
      if (!performance)
      {
        reply(c, 503, {{"error", "production-performance-unavailable"}});
        return true;
      }
      reply(c, 200, *performance);
    }
    else if (path == "/v1/compositions")
      reply(c, 200, runtime.state().generic_compositions(runtime.thumbnails_available()));
    else if (path == "/v1/transitions")
      reply(c, 200, runtime.state().generic_transitions());
    else if (path == "/v1/canvases" || path == "/v1/scene-collections")
    {
      const auto snapshot = runtime.state().production_catalogs();
      const auto value = path == "/v1/canvases" ? production_canvas_json(snapshot)
                                                : production_collection_catalog_json(snapshot);
      if (!runtime.obs().connected())
        reply(c, 503, {{"error", "production-backend-unavailable"}});
      else if (value)
        reply(c, 200, *value);
      else
        reply(
            c,
            503,
            {{"error",
              path == "/v1/canvases" ? "canvases-unavailable" : "scene-collections-unavailable"}});
    }
    else if (path == "/v1/obs/profiles")
    {
      const auto outcome = profile_catalog_response(runtime.obs_profiles(device));
      reply(c, outcome.status, outcome.body);
    }
    else if (path == "/v1/obs/scene-collections")
    {
      const auto outcome = scene_collection_catalog_response(runtime.obs_scene_collections(device));
      reply(c, outcome.status, outcome.body);
    }
    else if (path == "/v1/streaming-destinations")
    {
      if (!runtime.state().streaming_destinations_available())
      {
        reply(c, 503, {{"error", "production-backend-unavailable"}});
        return true;
      }
      auto value = runtime.state().generic_streaming_destinations();
      value["productionProvider"] = device.production_provider;
      value["backendStatus"] = backend_status(device);
      reply(c, 200, value);
    }
    else if (path == "/v1/hotkeys")
    {
      if (device.role < Role::operator_ && runtime.obs().backend_info().capabilities.system_hotkeys)
      {
        reply(c, 403, {{"error", "operator_role_required"}});
        return true;
      }
      if (!runtime.obs().hotkey_catalog_supported())
      {
        reply(c,
              200,
              {{"type", "hotkeyList"},
               {"productionProvider", device.production_provider},
               {"supported", false},
               {"hotkeys", json::array()}});
        return true;
      }
      if (!runtime.obs().connected())
      {
        reply(c, 503, {{"error", "production-backend-unavailable"}});
        return true;
      }
      std::optional<std::vector<ProductionHotkeyDefinition>> hotkeys;
      try
      {
        hotkeys = runtime.obs().hotkeys();
      }
      catch (const std::exception&)
      {
        if (endurance_log_ && endurance_log_->enabled())
          endurance_log_->record(
              "hotkeys", "catalogReadFailed", {{"productionProvider", device.production_provider}});
        reply(c, 503, {{"error", "production-hotkey-catalog-unavailable"}});
        return true;
      }
      if (!hotkeys)
      {
        reply(c, 503, {{"error", "production-hotkey-catalog-unavailable"}});
        return true;
      }
      json definitions = json::array();
      for (const auto& hotkey : *hotkeys)
      {
        json definition = {{"id", hotkey.id}, {"name", hotkey.name}, {"keys", hotkey.keys}};
        if (!hotkey.bindings.empty())
          definition["bindings"] = hotkey.bindings;
        definitions.push_back(std::move(definition));
      }
      reply(c,
            200,
            {{"type", "hotkeyList"},
             {"productionProvider", device.production_provider},
             {"supported", true},
             {"hotkeys", std::move(definitions)}});
    }
    else if (path == "/v1/audio-inputs")
      reply(c, 200, runtime.state().generic_audio_inputs());
    else if (path == "/v1/critical-queue")
    {
      if (device.role < Role::operator_)
      {
        reply(c, 403, {{"error", "operator_role_required"}});
        return true;
      }
      reply(c, 200, runtime.critical_queue_health());
    }
    else if (path.starts_with("/v1/critical-actions/"))
    {
      constexpr std::string_view prefix = "/v1/critical-actions/";
      const auto action_id = path.substr(prefix.size());
      if (action_id.empty())
      {
        reply(c, 400, {{"error", "action_id_required"}});
        return true;
      }
      const auto value = runtime.critical_action_status(action_id, device);
      reply(c, value.at("reason") == "unauthorized" ? 403 : 200, value);
    }
    else if (path == "/v1/admin/diagnostics")
    {
      if (device.role < Role::admin)
      {
        reply(c, 403, {{"error", "admin_role_required"}});
        return true;
      }
      json devices = json::array();
      for (const auto& value : runtime.audit().devices())
        devices.push_back({{"id", value.id},
                           {"name", value.name},
                           {"role", role_name(value.role)},
                           {"productionProvider", value.production_provider},
                           {"lastSeenUnixMs", value.last_seen_unix_ms}});
      const auto websocket = websocket_frames_.diagnostics();
      reply(c,
            200,
            {{"gateway",
              {{"instanceId", config_.identity.instance_id},
               {"name", config_.identity.name},
               {"certificateFingerprint", config_.identity.certificate_fingerprint},
               {"gatewayVersion", PROJECT_VERSION},
               {"protocolVersion", "1.0.0"},
               {"pluginIpcCompatibility", ">=1.0.0 <2.0.0"}}},
             {"productionProvider", device.production_provider},
             {"backendStatus", backend_status(device)},
             {"state", runtime.state().snapshot()},
             {"webrtc", session.webrtc->health()},
             {"thumbnails", runtime.thumbnail_diagnostics()},
             {"audioMixer", runtime.audio_mixer_diagnostics()},
             {"sceneItemProperties", runtime.scene_item_properties_diagnostics()},
             {"criticalLiveControl", runtime.critical_queue_health()},
             {"networkServer", diagnostics()},
             {"websocket",
              {{"connectionsOpened", websocket.connections_opened},
               {"connectionsClosed", websocket.connections_closed},
               {"peerCloseFrames", websocket.peer_close_frames},
               {"pingsReceived", websocket.pings_received},
               {"pongsSent", websocket.pongs_sent},
               {"transportErrors", websocket.transport_errors},
               {"lastPingUnixMs", websocket.last_ping_unix_ms},
               {"lastPongUnixMs", websocket.last_pong_unix_ms},
               {"lastPeerCloseCode", websocket.last_peer_close_code},
               {"lastPeerCloseReason", websocket.last_peer_close_reason}}},
             {"devices", devices},
             {"recentCommands", runtime.audit().recent_commands()},
             {"commandDiagnosticsDropped", runtime.audit().dropped_command_diagnostics()}});
    }
    else if (path.starts_with("/v1/resources/") && path.ends_with("/thumbnail"))
    {
      handle_thumbnail(c, device, path, ThumbnailTarget::source);
    }
    else if (path.starts_with("/v1/compositions/") && path.ends_with("/thumbnail"))
    {
      handle_thumbnail(c, device, path, ThumbnailTarget::scene);
    }
    else if (path.starts_with("/v1/compositions/") && path.ends_with("/layers"))
    {
      constexpr std::string_view prefix = "/v1/compositions/";
      constexpr std::string_view suffix = "/layers";
      auto id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
      reply(c, 200, runtime.state().generic_layers(id, runtime.thumbnails_available()));
    }
    else
    {
      reply(c, 404, {{"error", "not_found"}});
    }
    return true;
  }
  catch (const CredentialStorageBusy&)
  {
    reply(c, 503, {{"error", "credential-storage-unavailable"}, {"retryable", true}});
    return true;
  }
  catch (const std::exception& e)
  {
    reply(c, 400, {{"error", std::string(e.what()).substr(0, 240)}});
    return true;
  }
}
bool GatewayHttpServer::handlePost(CivetServer*, mg_connection* c)
{
  if (uri(c).starts_with("/v1/remote-tallies"))
    return handle_tally_request(c, HttpRequestMethod::post);
  http_requests_.fetch_add(1, std::memory_order_relaxed);
  const auto active = active_http_requests_.fetch_add(1, std::memory_order_relaxed) + 1;
  auto maximum = maximum_active_http_requests_.load(std::memory_order_relaxed);
  while (maximum < active && !maximum_active_http_requests_.compare_exchange_weak(
                                 maximum, active, std::memory_order_relaxed))
  {
  }
  struct RequestGuard
  {
    std::atomic<std::uint64_t>& count;
    ~RequestGuard()
    {
      count.fetch_sub(1, std::memory_order_relaxed);
    }
  } guard{active_http_requests_};
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record(
        "http", "requestStarted", {{"method", "POST"}, {"route", uri(c)}, {"active", active}});
  try
  {
    auto path = uri(c);
    const auto scope = http_request_scope(HttpRequestMethod::post, path);
    if (scope == RequestScope::pairing)
    {
      const auto admission = permit_public(c, scope);
      if (admission.retry_after_ms)
      {
        reply_rate_limited(c, request_scope_name(scope), admission.retry_after_ms);
        return true;
      }
    }
    if (path == "/v1/pairing/exchange")
    {
      try
      {
        const auto body = read_json(c);
        if (endurance_log_ && endurance_log_->enabled())
          endurance_log_->record("pairing", "exchangeStarted");
        const auto parsed = parse_pairing_exchange_request(body);
        if (!parsed.request)
        {
          const auto reason = parsed.failure == PairingExchangeRequestFailure::invalid_device_info
                                  ? "invalid_device_info"
                                  : "invalid_pairing_request";
          if (endurance_log_ && endurance_log_->enabled())
            endurance_log_->record("pairing", "exchangeRejected", {{"reason", reason}});
          reply(c, 400, {{"error", reason}, {"reason", reason}});
          return true;
        }
        auto response = pairing_.exchange(parsed.request->pairing_secret,
                                          parsed.request->device_name,
                                          parsed.request->production_provider,
                                          parsed.request->device_info);
        Device paired_device;
        paired_device.production_provider = response.value("productionProvider", "");
        response["backendStatus"] = backend_status(paired_device);
        if (endurance_log_ && endurance_log_->enabled())
          endurance_log_->record(
              "pairing",
              "exchangeSucceeded",
              {{"deviceId", response.value("deviceId", "")}, {"role", response.value("role", "")}});
        reply(c, 200, response);
      }
      catch (const PairingExchangeError& error)
      {
        const auto reason =
            std::string(authentication_diagnostics::pairing_failure_reason(error.failure()));
        const int status =
            error.failure() == PairingExchangeFailure::invalid_device_name ||
                    error.failure() == PairingExchangeFailure::invalid_production_provider ||
                    error.failure() == PairingExchangeFailure::invalid_device_info
                ? 400
                : 401;
        if (endurance_log_ && endurance_log_->enabled())
          endurance_log_->record("pairing", "exchangeRejected", {{"reason", reason}});
        const auto error_code =
            error.failure() == PairingExchangeFailure::invalid_production_provider
                ? "invalid_production_provider"
            : error.failure() == PairingExchangeFailure::invalid_device_info ? "invalid_device_info"
            : error.failure() == PairingExchangeFailure::invalid_device_name
                ? "invalid_device_name"
                : "invalid_pairing_secret";
        // Keep lease state in local diagnostics; unauthenticated clients receive one outcome.
        reply(c, status, {{"error", error_code}, {"reason", status == 401 ? error_code : reason}});
      }
      return true;
    }
    auto authorization = authorize(c, "http");
    if (!authorization.device)
    {
      const auto admission = permit_public(c, RequestScope::authentication);
      if (admission.retry_after_ms)
        reply_rate_limited(c, "authentication", admission.retry_after_ms);
      else
        reply(c, 401, authentication_failure_response(authorization.failure_reason));
      return true;
    }
    const auto admission = permit_device(*authorization.device, scope);
    if (admission.retry_after_ms)
    {
      reply_rate_limited(c, request_scope_name(scope), admission.retry_after_ms);
      return true;
    }
    auto body = read_json(c);
    // The client controls how long body delivery takes. Do not carry the earlier credential
    // snapshot across that wait into a privileged operation or an idempotent replay.
    authorization = authorize(c, "http");
    if (!authorization.device)
    {
      reply_authentication_failure(c, authorization.failure_reason);
      return true;
    }
    const auto& device = *authorization.device;
    auto& selected_runtime = *provider_session(device).runtime;
    if (selected_runtime.obs().backend_info().kind == ProductionBackendKind::companion_teleprompter)
    {
      const bool prompting = teleprompter_session_command_path(path) ||
                             teleprompter_session_position_path(path) ||
                             teleprompter_progress_path(path);
      if (prompting && !selected_runtime.running())
      {
        reply(c, 503, {{"error", "teleprompter-unavailable"}});
        return true;
      }
      if (!prompting && path != "/v1/device/rename" && path != "/v1/role-change-requests")
      {
        reply(c, 409, {{"error", "capability-unavailable"}});
        return true;
      }
    }
    if (teleprompter_session_command_path(path))
    {
      const auto response = teleprompter_session_command(
          teleprompter_ ? teleprompter_->shared_sessions() : nullptr, device.id, path, body);
      if (response.status == 200)
        publish_teleprompter_session(
            device.id, response.body.at("session").at("id").get<std::string>(), false);
      reply(c, response.status, response.body);
      return true;
    }
    if (teleprompter_session_position_path(path))
    {
      const auto response = teleprompter_session_position(
          teleprompter_ ? teleprompter_->shared_sessions() : nullptr, device.id, path, body);
      if (response.status == 200)
        publish_teleprompter_session(
            device.id, response.body.at("session").at("id").get<std::string>(), true);
      reply(c, response.status, response.body);
      return true;
    }
    if (teleprompter_progress_path(path))
    {
      const auto response = teleprompter_report(teleprompter_, device.id, path, body);
      reply(c, response.status, response.body);
      return true;
    }
    if (path == "/v1/hotkeys/trigger")
    {
      const auto parsed = parse_hotkey_request(body);
      if (!parsed.request)
      {
        reply(c, 400, {{"error", "invalid_hotkey_request"}, {"reason", parsed.reason}});
        return true;
      }
      const auto result = hotkeys_.trigger(device, *parsed.request);
      json keys = json::array();
      for (const auto& key : parsed.request->keys)
        keys.push_back(hotkey_key_name(key));
      const bool completed = result.status == HotkeyTriggerStatus::completed ||
                             result.status == HotkeyTriggerStatus::already_completed;
      const int status = result.status == HotkeyTriggerStatus::permission_denied ? 403
                         : result.status == HotkeyTriggerStatus::unsupported     ? 422
                         : result.status == HotkeyTriggerStatus::unavailable     ? 503
                         : result.status == HotkeyTriggerStatus::in_progress     ? 202
                                                                                 : 200;
      const auto response_status = completed ? "completed"
                                   : result.status == HotkeyTriggerStatus::in_progress
                                       ? "in-progress"
                                       : "rejected";
      json response = {
          {"type", "hotkeyTriggerResult"},
          {"requestId", parsed.request->request_id},
          {"status", response_status},
          {"target", parsed.request->target == HotkeyTarget::system ? "system" : "production"},
          {"keys", std::move(keys)},
          {"replayed", result.status == HotkeyTriggerStatus::already_completed}};
      if (!parsed.request->hotkey_id.empty())
        response["hotkeyId"] = parsed.request->hotkey_id;
      if (!result.reason.empty())
        response["reason"] = result.reason;
      reply(c, status, response);
      return true;
    }
    auto& session = provider_session(device);
    auto& runtime = *session.runtime;
    if (path == "/v1/scene-collections/current")
    {
      const auto selection = parse_production_collection_selection(body);
      if (!selection)
      {
        reply(c, 400, {{"error", "invalid-scene-collection-selection"}});
        return true;
      }
      const auto response = runtime.select_collection(device, *selection);
      reply(c,
            response.value("status", "") == "accepted"
                ? 202
                : production_collection_error_status(response.value("error", "")),
            response);
      return true;
    }
    if (path == "/v1/obs/profiles/current")
    {
      const auto selection = parse_obs_profile_selection(body);
      if (!selection)
      {
        reply(c, 400, {{"error", "invalid-obs-profile-selection"}});
        return true;
      }
      const auto outcome =
          profile_selection_response(runtime.select_obs_profile(device, *selection));
      reply(c, outcome.status, outcome.body);
      return true;
    }
    if (path == "/v1/obs/scene-collections/current")
    {
      const auto selection = parse_obs_scene_collection_selection(body);
      if (!selection)
      {
        reply(c, 400, {{"error", "invalid-obs-scene-collection-selection"}});
        return true;
      }
      const auto outcome = scene_collection_selection_response(
          runtime.select_obs_scene_collection(device, *selection));
      reply(c, outcome.status, outcome.body);
      return true;
    }
    if (path == "/v1/webrtc/recovery")
    {
      const auto parsed = parse_webrtc_recovery_request(body);
      if (!parsed.request)
      {
        reply(c,
              400,
              {{"type", "webrtcSessionRecovery"},
               {"status", "invalid-request"},
               {"reason", parsed.reason}});
        return true;
      }
      const auto device_session_key = device.production_provider + ":" + device.id;
      const auto outcome =
          webrtc_recovery_.recover(device_session_key,
                                   *parsed.request,
                                   [&]
                                   {
                                     return session.webrtc->retire_device_session(
                                         device.id, parsed.request->media_session_id);
                                   });
      if (endurance_log_ && endurance_log_->enabled())
        endurance_log_->record("webrtc",
                               "clientRecoveryResult",
                               {{"deviceId", device.id},
                                {"productionProvider", device.production_provider},
                                {"recoveryId", parsed.request->recovery_id},
                                {"mediaSessionId", parsed.request->media_session_id},
                                {"status", outcome.body.value("status", "failed")},
                                {"httpStatus", outcome.http_status},
                                {"replayed", outcome.body.value("replayed", false)}});
      reply(c, outcome.http_status, outcome.body);
      return true;
    }
    if (path == "/v1/role-change-requests")
    {
      const auto request_id = body.value("requestId", "");
      const auto requested_role_name = body.value("role", "");
      if (request_id.empty() || request_id.size() > 128)
      {
        reply(c, 400, {{"error", "request_id_required"}});
        return true;
      }
      if (requested_role_name != "viewer" && requested_role_name != "operator")
      {
        reply(c, 400, {{"error", "invalid_requested_role"}});
        return true;
      }
      const auto outcome =
          execute_role_extension(device, request_id, parse_role(requested_role_name));
      reply(c, outcome.http_status, outcome.body);
      return true;
    }
    if (path == "/v1/device/rename")
    {
      if (!body.contains("name") || !body["name"].is_string())
      {
        reply(c, 400, {{"error", "invalid_device_name"}});
        return true;
      }
      const auto name = body["name"].get<std::string>();
      if (!valid_device_name(name))
      {
        reply(c, 400, {{"error", "invalid_device_name"}});
        return true;
      }
      if (!runtime.audit().rename_device(device.id, name))
      {
        reply(c, 401, {{"error", "unauthorized"}, {"reason", "device_not_found_or_revoked"}});
        return true;
      }
      runtime.audit().command(device.id, "rename-device", device.id, "accepted", "");
      {
        std::lock_guard lock(sockets_mutex_);
        for (auto& [connection, connected_device] : socket_devices_)
        {
          static_cast<void>(connection);
          if (connected_device.id == device.id)
            connected_device.name = name;
        }
      }
      reply(c, 200, device_renamed_response(device.id, name));
      return true;
    }
    constexpr std::string_view admin_prefix = "/v1/admin/devices/";
    if (path.starts_with(admin_prefix))
    {
      if (device.role < Role::admin)
      {
        reply(c, 403, {{"error", "admin_role_required"}});
        return true;
      }
      auto tail = path.substr(admin_prefix.size());
      auto slash = tail.rfind('/');
      if (slash == std::string::npos)
      {
        reply(c, 404, {{"error", "not_found"}});
        return true;
      }
      auto target = tail.substr(0, slash);
      auto action = tail.substr(slash + 1);
      if (target.empty())
      {
        reply(c, 400, {{"error", "device_id_required"}});
        return true;
      }
      if (action == "revoke")
      {
        runtime.audit().revoke_device(target);
        disconnect_device(target);
        runtime.audit().command(device.id, "revoke-device", target, "accepted", "");
        reply(c, 200, {{"deviceId", target}, {"revoked", true}});
        return true;
      }
      if (action == "role")
      {
        const auto role_field = body.find("role");
        if (role_field == body.end() || !role_field->is_string() ||
            (*role_field != "viewer" && *role_field != "operator" && *role_field != "admin"))
        {
          reply(c, 400, {{"error", "invalid_device_role"}});
          return true;
        }
        const auto role = parse_role(role_field->get_ref<const std::string&>());
        runtime.audit().set_role(target, role);
        runtime.audit().command(device.id, "set-device-role", target, "accepted", "");
        reply(c, 200, {{"deviceId", target}, {"role", role_name(role)}});
        return true;
      }
      reply(c, 404, {{"error", "not_found"}});
      return true;
    }
    if (path == "/v1/critical-actions")
    {
      const auto outcome = runtime.submit_critical_action(device, body);
      reply(c, outcome.http_status, outcome.body);
      return true;
    }
    if (path == "/v1/critical-controls/acknowledge-block")
    {
      const auto outcome = runtime.acknowledge_critical_block(device, body);
      reply(c, outcome.http_status, outcome.body);
      return true;
    }
    if (path == "/v1/transitions/current")
    {
      const auto outcome = runtime.set_current_transition(device, body);
      reply(c, outcome.http_status, outcome.body);
      return true;
    }
    if (path == "/v1/transitions/default")
    {
      const auto outcome = runtime.set_default_transition(device, body);
      reply(c, outcome.http_status, outcome.body);
      return true;
    }
    constexpr std::string_view transition_prefix = "/v1/transitions/";
    if (path.starts_with(transition_prefix))
    {
      const auto transition_id = path.substr(transition_prefix.size());
      if (transition_id.empty() || transition_id.find('/') != std::string_view::npos)
      {
        reply(c, 404, {{"error", "not_found"}});
        return true;
      }
      body["transitionId"] = transition_id;
      const auto outcome = runtime.configure_transition(device, body);
      reply(c, outcome.http_status, outcome.body);
      return true;
    }
    if (path == "/v1/webrtc/offer")
    {
      std::optional<std::string> media_reset_id;
      if (const auto reset = body.find("mediaResetId"); reset != body.end())
      {
        if (!reset->is_string() || reset->get_ref<const std::string&>().empty() ||
            reset->get_ref<const std::string&>().size() > 128)
        {
          reply(c, 400, {{"error", "invalid_media_reset_id"}});
          return true;
        }
        media_reset_id = reset->get<std::string>();
      }
      const auto reset_validation = media_session_resets_.validate_offer(device.id, media_reset_id);
      if (reset_validation.disposition != MediaSessionResetOfferDisposition::not_required &&
          reset_validation.disposition != MediaSessionResetOfferDisposition::accepted)
      {
        json error = {{"error", "media_session_reset_required"}};
        if (reset_validation.expected)
        {
          error["mediaResetId"] = reset_validation.expected->id;
          error["captureGeneration"] = reset_validation.expected->capture_generation;
        }
        reply(c, 409, error);
        return true;
      }
      const VideoOfferPreferences preferences{read_video_preferences(body, "program"),
                                              read_video_preferences(body, "preview"),
                                              audio_monitor_requested(body),
                                              audio_master_requested(body)};
      bool reset_changed_during_admission = false;
      const auto result = session.webrtc->answer(
          device.id,
          body.value("sdp", ""),
          preferences,
          [&]
          {
            if (!runtime.audit().active_device_role(device.id))
              return false;
            const auto current = media_session_resets_.validate_offer(device.id, media_reset_id);
            reset_changed_during_admission =
                current.disposition != MediaSessionResetOfferDisposition::not_required &&
                current.disposition != MediaSessionResetOfferDisposition::accepted;
            return !reset_changed_during_admission;
          });
      if (const auto failure = std::get_if<MediaOfferFailure>(&result))
      {
        const auto current = authorize(c, "http");
        if (!current.device)
          reply(c, 401, authentication_failure_response(current.failure_reason));
        else if (*failure == MediaOfferFailure::capture_reset || reset_changed_during_admission)
        {
          json error = {
              {"error",
               media_reset_id ? "media_session_reset_superseded" : "media_session_reset_required"}};
          if (const auto reset = media_session_resets_.pending(device.id))
          {
            error["mediaResetId"] = reset->id;
            error["captureGeneration"] = reset->capture_generation;
          }
          reply(c, 409, error);
        }
        else if (*failure == MediaOfferFailure::transport_closed)
          reply(c, 400, {{"error", "WebRTC transport closed during negotiation"}});
        else if (*failure == MediaOfferFailure::sdp_limits_exceeded)
          reply(c, 400, {{"error", "invalid-sdp-limits"}});
        else if (*failure == MediaOfferFailure::invalid_sdp)
          reply(c, 400, {{"error", "invalid-sdp"}});
        else
          reply(c, 503, {{"error", "production-backend-unavailable"}});
        return true;
      }
      const auto& answer = std::get<json>(result);
      json response = {{"sdp", answer.value("sdp", "")},
                       {"mediaSessionId", answer.value("mediaSessionId", "")},
                       {"program", answer.value("program", false)},
                       {"preview", answer.value("preview", false)},
                       {"audioMonitor", answer.value("audioMonitor", false)},
                       {"audioMaster", answer.value("audioMaster", false)}};
      if (const auto reason = answer.find("audioMonitorUnavailableReason"); reason != answer.end())
        response["audioMonitorUnavailableReason"] = *reason;
      if (const auto reason = answer.find("audioMasterUnavailableReason"); reason != answer.end())
        response["audioMasterUnavailableReason"] = *reason;
      if (media_reset_id)
      {
        if (!media_session_resets_.complete_offer(device.id, *media_reset_id))
        {
          session.webrtc->retire_device_session(device.id,
                                                response.value("mediaSessionId", std::string{}));
          json error = {{"error", "media_session_reset_superseded"}};
          if (const auto pending = media_session_resets_.pending(device.id))
          {
            error["mediaResetId"] = pending->id;
            error["captureGeneration"] = pending->capture_generation;
          }
          reply(c, 409, error);
          return true;
        }
        response["mediaResetId"] = *media_reset_id;
      }
      reply(c, 200, response);
      return true;
    }
    if (path == "/v1/commands")
    {
      const auto outcome = execute_generic_command(device, body);
      reply(c, outcome.http_status, outcome.body);
      return true;
    }
    reply(c, 404, {{"error", "not_found"}});
    return true;
  }
  catch (const CredentialStorageBusy&)
  {
    reply(c, 503, {{"error", "credential-storage-unavailable"}, {"retryable", true}});
    return true;
  }
  catch (const std::exception& e)
  {
    reply(c, 400, {{"error", std::string(e.what()).substr(0, 240)}});
    return true;
  }
}
bool GatewayHttpServer::handleConnection(CivetServer*, const mg_connection* c)
try
{
  auto authorization = authorize(c, "websocket");
  if (!authorization.device)
  {
    const auto admission = permit_public(c, RequestScope::authentication);
    if (endurance_log_ && endurance_log_->enabled())
      endurance_log_->record("websocket",
                             "connectionRejected",
                             {{"reason", admission.retry_after_ms ? "rateLimit" : "authorization"},
                              {"authFailure", authorization.failure_reason}});
    return false;
  }
  const auto& device = *authorization.device;
  auto admission = permit_device(device, RequestScope::websocket);
  if (admission.retry_after_ms)
  {
    if (endurance_log_ && endurance_log_->enabled())
      endurance_log_->record(
          "websocket", "connectionRejected", {{"reason", "rateLimit"}, {"deviceId", device.id}});
    reply_websocket_rate_limited(const_cast<mg_connection*>(c), admission.retry_after_ms);
    return false;
  }
  std::lock_guard lock(sockets_mutex_);
  websocket_reservations_.emplace(const_cast<mg_connection*>(c), std::move(admission.reservation));
  socket_devices_[const_cast<mg_connection*>(c)] = device;
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record("websocket",
                           "connectionAuthorized",
                           {{"deviceId", device.id}, {"role", role_name(device.role)}});
  return true;
}
catch (const CredentialStorageBusy&)
{
  reply(const_cast<mg_connection*>(c),
        503,
        {{"error", "credential-storage-unavailable"}, {"retryable", true}});
  return false;
}
catch (const std::exception&)
{
  record_websocket_boundary_failure(endurance_log_, "connectionAdmissionFailed", "internal-error");
  return false;
}
catch (...)
{
  record_websocket_boundary_failure(endurance_log_, "connectionAdmissionFailed", "unknown");
  return false;
}
void GatewayHttpServer::handleReadyState(CivetServer*, mg_connection* c)
{
  try
  {
    websocket_frames_.connection_opened();
    {
      std::lock_guard lock(sockets_mutex_);
      sockets_.insert(c);
    }
    std::optional<Device> device;
    {
      std::lock_guard lock(sockets_mutex_);
      const auto found = socket_devices_.find(c);
      if (found != socket_devices_.end())
        device = found->second;
    }
    if (!device)
      return;

    if (!open_client_events(c, *device))
      return;

    if (remote_tallies_ && device->production_provider == "obs")
      remote_tallies_->refresh_authorization(device->id);

    if (endurance_log_ && endurance_log_->enabled())
      endurance_log_->record("websocket",
                             "connectionReady",
                             {{"deviceId", device->id}, {"role", role_name(device->role)}});
    auto hello = json{{"type", "hello"},
                      {"apiVersion", 1},
                      {"contractRevision", 2},
                      {"deviceId", device->id},
                      {"role", role_name(device->role)},
                      {"productionProvider", device->production_provider},
                      {"backendStatus", backend_status(*device)}};
    if (remote_tallies_ && device->production_provider == "obs")
      hello["remoteTallyPublication"] = {{"version", 1}};
    if (mg_get_user_connection_data(c))
    {
      hello["countdownRelay"] = {{"version", 1},
                                 {"adapterScopeId", provider_session(*device).event_scope_id}};
      hello["clientIdentification"] = {{"version", 1}};
    }
    write_text(c, hello.dump());
    const auto catalogs = provider_session(*device).runtime->state().production_catalogs();
    for (const auto& replacement : production_catalog_replacements(catalogs))
      write_text(c, replacement.dump());
    const auto current_capabilities = capabilities(*device, catalogs);
    write_text(c,
               json{{"type", "capabilityReplacement"},
                    {"contractRevision", 2},
                    {"capabilityRevision", current_capabilities["capabilityRevision"]},
                    {"features", current_capabilities["features"]}}
                   .dump());
    write_text(c, state_replacement(*device).dump());
    write_text(c, backend_status(*device).dump());
    write_text(c, composition_replacement(*device).dump());
    write_text(c, transition_replacement(*device).dump());
    if (const auto replacement = streaming_destination_replacement(*device))
      write_text(c, replacement->dump());
    for (const auto& layers : layer_replacements(*device))
      write_text(c, layers.dump());
    write_text(c, audio_input_replacement(*device).dump());
    if (const auto reset = media_session_resets_.pending(device->id))
      write_text(c, media_session_reset_required_message(*reset).dump());
    if (auto* channel = static_cast<ClientEventConnection*>(mg_get_user_connection_data(c)))
      channel->activate();
  }
  catch (const std::exception& error)
  {
    record_websocket_boundary_failure(endurance_log_, "connectionBootstrapFailed", error.what());
  }
  catch (...)
  {
    record_websocket_boundary_failure(endurance_log_, "connectionBootstrapFailed", "unknown");
  }
}
bool GatewayHttpServer::handleData(
    CivetServer*, mg_connection* c, int bits, char* data, size_t length)
{
  auto* event_connection = static_cast<ClientEventConnection*>(mg_get_user_connection_data(c));
  // The native reader owns this borrowed channel until handleClose. Reject buffered input after
  // revocation or delivery failure before parsing, replying to pings, or dispatching commands.
  if (!event_connection || !event_connection->ready())
    return false;
  // A Close is already terminal. Bound every other frame before parsing or Ping diagnostics,
  // using receive-thread-local credits so notifications still bypass application mutexes.
  if ((bits & 0x0f) != MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE &&
      !event_connection->admit_frame(length, Clock::now()))
  {
    event_connection->revoke();
    constexpr std::string_view payload = "\x03\xf0"
                                         "Client frame budget exceeded";
    mg_websocket_write(c, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, payload.data(), payload.size());
    return false;
  }
  const auto disposition = websocket_frames_.route(
      bits,
      std::string_view(data, length),
      [c](int opcode, std::string_view payload)
      { return mg_websocket_write(c, opcode, payload.data(), payload.size()); });
  if (disposition == WebSocketFrameDisposition::close_connection)
    return false;
  if (disposition != WebSocketFrameDisposition::application_text)
    return true;
  try
  {
    handle_websocket_text(c, *event_connection, std::string_view(data, length));
  }
  catch (const std::exception&)
  {
    record_websocket_boundary_failure(
        endurance_log_, "applicationMessageRejected", "invalid-message");
  }
  catch (...)
  {
    record_websocket_boundary_failure(endurance_log_, "applicationMessageRejected", "unknown");
  }
  return true;
}
void GatewayHttpServer::handleClose(CivetServer*, const mg_connection* c)
{
  websocket_frames_.connection_closed();
  auto* connection = const_cast<mg_connection*>(c);
  std::optional<Device> device;
  ClientEventConnection* event_connection = nullptr;
  {
    std::lock_guard lock(sockets_mutex_);
    const auto found = socket_devices_.find(connection);
    if (found != socket_devices_.end())
      device = found->second;
    sockets_.erase(connection);
    socket_devices_.erase(connection);
    event_connection = static_cast<ClientEventConnection*>(mg_get_user_connection_data(c));
    mg_set_user_connection_data(c, nullptr);
  }
  client_events_.disconnect(event_connection);
  try
  {
    if (device)
      provider_session(*device).runtime->audit().mark_device_seen(device->id);
    if (endurance_log_ && endurance_log_->enabled())
      endurance_log_->record(
          "websocket", "connectionClosed", {{"deviceId", device ? device->id : ""}});
  }
  catch (const std::exception& error)
  {
    if (endurance_log_ && endurance_log_->enabled())
      endurance_log_->record("websocket",
                             "connectionCloseActivityFailed",
                             {{"deviceId", device ? device->id : ""},
                              {"error", std::string(error.what()).substr(0, 240)}});
  }
}
void GatewayHttpServer::broadcast(const json& message)
{
  queue_message(message.dump(), std::nullopt);
}

void GatewayHttpServer::reinitialize_media_sessions(std::string_view production_provider,
                                                    std::uint32_t capture_generation)
{
  WebRtcHub* media = nullptr;
  for (auto& session : provider_sessions_)
    if (session.id == production_provider)
    {
      media = session.webrtc;
      break;
    }
  if (!media)
    throw std::invalid_argument("unknown production provider for media reset");

  std::vector<std::string> device_ids;
  {
    std::lock_guard lock(sockets_mutex_);
    for (const auto& [connection, device] : socket_devices_)
    {
      static_cast<void>(connection);
      if (device.production_provider == production_provider)
        device_ids.push_back(device.id);
    }
  }
  const auto reset =
      media_session_resets_.begin(std::string(production_provider), capture_generation, device_ids);

  // Install correlation before retiring peers so an uncorrelated offer cannot cross generations.
  media->restart_video_sessions();
  queue_message(
      media_session_reset_required_message(reset).dump(), std::nullopt, reset.production_provider);
  if (endurance_log_ && endurance_log_->enabled())
    endurance_log_->record("webrtc",
                           "mediaSessionResetRequired",
                           {{"productionProvider", reset.production_provider},
                            {"captureGeneration", reset.capture_generation},
                            {"targetDeviceCount", device_ids.size()}});
}

void GatewayHttpServer::on_role_change_requested(RoleChangeRequestHandler handler)
{
  std::lock_guard lock(role_change_request_handler_mutex_);
  role_change_request_handler_ = std::move(handler);
}

void GatewayHttpServer::on_client_removed(std::function<void(std::string_view)> handler)
{
  std::lock_guard lock(client_removed_handler_mutex_);
  client_removed_handler_ = std::move(handler);
}

void GatewayHttpServer::on_global_setting_change_requested(
    GlobalSettingChangeRequestHandler handler)
{
  std::lock_guard lock(global_setting_change_request_handler_mutex_);
  global_setting_change_request_handler_ = std::move(handler);
}

void GatewayHttpServer::apply_role_changes(const std::vector<DeviceRoleChange>& changes)
{
  std::vector<DeviceRoleChange> applied;
  applied.reserve(changes.size());
  {
    std::lock_guard lock(sockets_mutex_);
    for (const auto& change : changes)
    {
      const bool superseded = std::any_of(socket_devices_.begin(),
                                          socket_devices_.end(),
                                          [&](const auto& entry)
                                          {
                                            return entry.second.id == change.device_id &&
                                                   entry.second.role_revision > change.revision;
                                          });
      if (superseded)
        continue;
      bool updated = false;
      for (auto& [connection, device] : socket_devices_)
      {
        static_cast<void>(connection);
        if (device.id == change.device_id && device.role_revision < change.revision)
        {
          device.role = change.role;
          device.role_revision = change.revision;
          if (auto* channel =
                  static_cast<ClientEventConnection*>(mg_get_user_connection_data(connection)))
            channel->set_permissions(
                change.role >= Role::operator_ ? std::numeric_limits<std::uint32_t>::max() : 0);
          updated = true;
        }
      }
      if (updated)
        applied.push_back(change);
    }
  }
  ++capability_revision_;
  for (const auto& change : applied)
    apply_role_change(change);
}

void GatewayHttpServer::apply_role_change(const DeviceRoleChange& change)
{
  std::optional<Device> candidate;
  {
    std::lock_guard lock(sockets_mutex_);
    for (const auto& [connection, device] : socket_devices_)
    {
      static_cast<void>(connection);
      if (device.id != change.device_id)
        continue;
      if (device.role_revision > change.revision)
        return;
      if (device.role_revision == change.revision)
        candidate = device;
    }
  }
  if (!candidate)
    return;
  const auto snapshot = provider_session(*candidate).runtime->state().production_catalogs();
  const auto catalogs = production_catalog_replacements(snapshot);
  const auto current = capabilities(*candidate, snapshot);
  auto replacement = json{{"type", "capabilityReplacement"},
                          {"contractRevision", 2},
                          {"capabilityRevision", 0},
                          {"features", current.at("features")}};
  const auto role_message = device_role_changed_message(change).dump();

  // Cached authority changes before potentially blocking provider projection. Recheck before
  // queuing events so a delayed projection cannot follow a newer role publication.
  std::lock_guard lock(sockets_mutex_);
  bool applicable = false;
  for (const auto& [connection, device] : socket_devices_)
  {
    static_cast<void>(connection);
    if (device.id != change.device_id)
      continue;
    if (device.role_revision > change.revision)
      return;
    applicable = applicable || device.role_revision == change.revision;
  }
  if (!applicable)
    return;
  replacement["capabilityRevision"] = capability_revision_.load();
  queue_message_locked(role_message, change.device_id, std::nullopt, ClientDeliveryPriority::state);
  for (const auto& catalog : catalogs)
    queue_message_locked(
        catalog.dump(), change.device_id, std::nullopt, ClientDeliveryPriority::state);
  queue_message_locked(
      replacement.dump(), change.device_id, std::nullopt, ClientDeliveryPriority::state);
}

void GatewayHttpServer::disconnect_device(std::string_view device_id)
{
  {
    const auto prefix = std::string(device_id) + ":";
    std::lock_guard lock(generic_command_cache_mutex_);
    const auto belongs_to_device = [&](const auto& entry)
    { return entry.first.starts_with(prefix); };
    std::erase_if(role_request_operations_, belongs_to_device);
    std::erase_if(global_setting_request_operations_, belongs_to_device);
    std::erase_if(generic_command_cache_, belongs_to_device);
  }
  std::function<void(std::string_view)> removed;
  {
    std::lock_guard lock(client_removed_handler_mutex_);
    removed = client_removed_handler_;
  }
  if (removed)
    removed(device_id);
  for (auto& session : provider_sessions_)
    session.webrtc->remove_device(device_id);
  std::lock_guard lock(sockets_mutex_);
  for (auto iterator = socket_devices_.begin(); iterator != socket_devices_.end();)
  {
    if (iterator->second.id != device_id)
    {
      ++iterator;
      continue;
    }
    auto* connection = iterator->first;
    if (auto* channel =
            static_cast<ClientEventConnection*>(mg_get_user_connection_data(connection)))
      channel->request_revocation_close();
    sockets_.erase(connection);
    iterator = socket_devices_.erase(iterator);
  }
}

void GatewayHttpServer::complete_global_setting_change_request(std::string_view device_id,
                                                               std::string_view request_id,
                                                               bool approved)
{
  std::string operation_id;
  {
    std::lock_guard lock(generic_command_cache_mutex_);
    const auto request_key = std::string(device_id) + ":" + std::string(request_id);
    if (const auto found = global_setting_request_operations_.find(request_key);
        found != global_setting_request_operations_.end())
    {
      if (Clock::now() - found->second.admitted_at < maximum_client_request_age)
        operation_id = found->second.operation_id;
      global_setting_request_operations_.erase(found);
    }
  }
  if (operation_id.empty())
    return;
  std::uint64_t state_sequence = 0;
  for (const auto& device : connected_devices())
    if (device.id == device_id)
      state_sequence = provider_session(device).runtime->state().revision();
  const auto message = approved ? json{{"type", "commandCompleted"},
                                       {"commandId", request_id},
                                       {"operationId", operation_id},
                                       {"resultingStateSequence", state_sequence}}
                                : json{{"type", "commandRejected"},
                                       {"commandId", request_id},
                                       {"operationId", operation_id},
                                       {"reason", "permission-required"},
                                       {"currentStateSequence", state_sequence}};
  queue_message(
      message.dump(), std::string(device_id), std::nullopt, ClientDeliveryPriority::control);
}

std::vector<Device> GatewayHttpServer::connected_devices() const
{
  std::lock_guard lock(sockets_mutex_);
  std::unordered_map<std::string, Device> unique_devices;
  for (const auto& [connection, device] : socket_devices_)
  {
    static_cast<void>(connection);
    unique_devices.insert_or_assign(device.id, device);
  }
  std::vector<Device> devices;
  devices.reserve(unique_devices.size());
  for (auto& [id, device] : unique_devices)
  {
    static_cast<void>(id);
    devices.push_back(std::move(device));
  }
  return devices;
}

json GatewayHttpServer::diagnostics() const
{
  const auto websocket = websocket_frames_.diagnostics();
  std::size_t authenticated_sockets = 0;
  std::size_t open_sockets = 0;
  std::size_t reserved_sockets = 0;
  {
    std::lock_guard lock(sockets_mutex_);
    authenticated_sockets = socket_devices_.size();
    open_sockets = sockets_.size();
    reserved_sockets = websocket_reservations_.size();
  }
  return {
      {"httpRequests", http_requests_.load(std::memory_order_relaxed)},
      {"httpResponses", http_responses_.load(std::memory_order_relaxed)},
      {"activeHttpRequests", active_http_requests_.load(std::memory_order_relaxed)},
      {"maximumActiveHttpRequests", maximum_active_http_requests_.load(std::memory_order_relaxed)},
      {"httpClientErrors", http_client_errors_.load(std::memory_order_relaxed)},
      {"httpServerErrors", http_server_errors_.load(std::memory_order_relaxed)},
      {"openWebSockets", open_sockets},
      {"authenticatedWebSockets", authenticated_sockets},
      {"reservedWebSockets", reserved_sockets},
      {"websocket",
       {{"connectionsOpened", websocket.connections_opened},
        {"connectionsClosed", websocket.connections_closed},
        {"peerCloseFrames", websocket.peer_close_frames},
        {"pingsReceived", websocket.pings_received},
        {"pongsSent", websocket.pongs_sent},
        {"transportErrors", websocket.transport_errors},
        {"lastPingUnixMs", websocket.last_ping_unix_ms},
        {"lastPongUnixMs", websocket.last_pong_unix_ms},
        {"lastPeerCloseCode", websocket.last_peer_close_code},
        {"lastPeerCloseReason", websocket.last_peer_close_reason}}}};
}
} // namespace studio_remote
