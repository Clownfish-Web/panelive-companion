#include "winhttp_obs_client.hpp"
#include "../credentials/windows_crypto.hpp"
#include "windows_obs_quick_transition_catalog.hpp"
#include <algorithm>
#include <studio_remote/obs/obs_hotkey_catalog.hpp>
#include <studio_remote/obs/obs_hotkey_sequence.hpp>
#include <studio_remote/obs/obs_performance_snapshot.hpp>
#include <studio_remote/obs/obs_rpc_request_exchange.hpp>

#include <bcrypt.h>
#include <studio_remote/obs/obs_reconnect.hpp>
#include <studio_remote/obs/obs_wire_protocol.hpp>

namespace studio_remote::windows
{
ObsClient::ObsClient(std::string host,
                     uint16_t port,
                     std::string password,
                     std::optional<std::filesystem::path> configuration_root)
    : configuration_root_(std::move(configuration_root)), host_(std::move(host)),
      password_(std::move(password)), port_(port)
{
}
ObsClient::~ObsClient()
{
  stop();
}

void ObsClient::start()
{
  if (thread_.joinable())
    return;
  {
    std::lock_guard lock(reconnect_mutex_);
    stopping_ = false;
  }
  thread_ = std::thread([this] { run(); });
}
void ObsClient::stop()
{
  if (reader_id_.load(std::memory_order_acquire) == GetCurrentThreadId())
    throw std::logic_error("OBS callbacks cannot stop their reader");
  {
    std::lock_guard lock(reconnect_mutex_);
    stopping_ = true;
  }
  close_socket();
  reconnect_cv_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void ObsClient::reconfigure(std::string host, uint16_t port, std::string password)
{
  {
    std::lock_guard lock(configuration_mutex_);
    host_ = std::move(host);
    port_ = port;
    password_ = std::move(password);
    std::lock_guard reconnect_lock(reconnect_mutex_);
    configuration_revision_.fetch_add(1, std::memory_order_release);
  }
  set_error("OBS connection settings changed; reconnecting");
  close_socket();
  reconnect_cv_.notify_all();
}

void ObsClient::run()
{
  reader_id_.store(GetCurrentThreadId(), std::memory_order_release);
  ObsReconnectBackoff backoff;
  while (!stopping_)
  {
    const auto configuration_revision = configuration_revision_.load(std::memory_order_acquire);
    std::optional<Clock::time_point> opened;
    try
    {
      if (connect_once(configuration_revision, opened))
        read_connected_messages();
    }
    catch (const std::exception& error)
    {
      set_error(error.what());
    }
    catch (...)
    {
      set_error("Unexpected OBS adapter failure");
    }
    bool was_connected = connected_.exchange(false);
    close_socket();
    pending_.cancel_all(std::make_exception_ptr(std::runtime_error("OBS connection lost")));
    if (was_connected)
      emit_connection(false, {});
    const auto age =
        opened ? std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *opened)
               : std::chrono::milliseconds::zero();
    const auto delay = backoff.next_delay(age, reconnect_random_sample());
    std::unique_lock reconnect_lock(reconnect_mutex_);
    const bool configuration_changed = reconnect_cv_.wait_for(
        reconnect_lock,
        delay,
        [this, configuration_revision]
        {
          return stopping_ ||
                 configuration_revision_.load(std::memory_order_acquire) != configuration_revision;
        });
    if (configuration_changed)
      backoff.reset();
  }
  reader_id_.store(0, std::memory_order_release);
}

void ObsClient::read_connected_messages()
{
  // WinHTTP does not expose PONG completion or an outstanding-PING limit. Keep this fallback
  // on the identified reader, using normal correlation and the existing reconnect cleanup.
  constexpr auto idle_interval = std::chrono::seconds(15);
  constexpr auto probe_timeout = std::chrono::seconds(30);
  auto deadline = Clock::now() + idle_interval;
  std::future<json> probe;
  while (!stopping_)
  {
    const auto remaining =
        std::max(std::chrono::milliseconds::zero(),
                 std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()));
    auto result = socket_.receive(std::min(remaining, std::chrono::milliseconds(5'000)));
    if (result.state == ObsSocketReadState::closed)
    {
      if (const auto error = socket_.last_error(); !error.empty())
        set_error(error);
      return;
    }
    if (result.state == ObsSocketReadState::message)
    {
      dispatch(result.message);
      if (!probe.valid())
        deadline = Clock::now() + idle_interval;
    }
    if (probe.valid() &&
        probe.wait_for(std::chrono::milliseconds::zero()) == std::future_status::ready)
    {
      static_cast<void>(probe.get());
      deadline = Clock::now() + idle_interval;
    }
    if (Clock::now() < deadline || stopping_)
      continue;
    if (probe.valid())
    {
      set_error("OBS WebSocket liveness probe timed out");
      return;
    }
    if (pending_.full())
    {
      deadline = Clock::now() + std::chrono::seconds(1);
      continue;
    }
    const auto id = WindowsCrypto::uuid_v4();
    probe = pending_.register_request(id);
    if (!send_message(make_obs_request("GetVersion", id, json::object())))
    {
      set_error("Could not send OBS WebSocket liveness probe");
      return;
    }
    deadline = Clock::now() + probe_timeout;
  }
}

bool ObsClient::connect_once(std::uint64_t configuration_revision,
                             std::optional<Clock::time_point>& opened)
{
  std::string configured_host;
  std::string configured_password;
  std::uint16_t configured_port = 0;
  {
    std::lock_guard lock(configuration_mutex_);
    configured_host = host_;
    configured_port = port_;
    configured_password = password_;
  }
  if (!socket_.connect(configured_host, configured_port, std::chrono::seconds(5)))
  {
    set_error(socket_.last_error());
    return false;
  }
  if (stopping_ ||
      configuration_revision_.load(std::memory_order_acquire) != configuration_revision)
  {
    close_socket();
    return false;
  }
  auto hello_text = receive_message(std::chrono::seconds(5));
  if (!hello_text)
  {
    set_error("OBS closed before Hello");
    return false;
  }
  const auto hello = parse_obs_hello(*hello_text, configured_password);
  if (!send_message(hello.identify_message))
  {
    set_error("Could not send OBS Identify");
    return false;
  }
  auto identified_text = receive_message(std::chrono::seconds(5));
  if (!identified_text)
  {
    set_error("OBS rejected or closed during Identify");
    return false;
  }
  validate_obs_identified(*identified_text);
  {
    // Commit against the same lock that changes the reconnect predicate. Cancellation must
    // not publish an obsolete connection between checking its revision and setting this state.
    std::lock_guard lock(reconnect_mutex_);
    if (stopping_ ||
        configuration_revision_.load(std::memory_order_acquire) != configuration_revision)
      return false;
    connected_ = true;
  }
  set_error({});
  opened = Clock::now();
  emit_connection(true, hello.version);
  return true;
}

std::optional<std::string> ObsClient::receive_message(std::chrono::milliseconds timeout)
{
  const auto deadline = Clock::now() + timeout;
  while (!stopping_)
  {
    const auto interval =
        timeout == std::chrono::milliseconds::zero()
            ? std::chrono::seconds(5)
            : std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    if (interval < std::chrono::milliseconds::zero())
      return std::nullopt;
    auto result = socket_.receive(interval);
    if (result.state == ObsSocketReadState::message)
      return std::move(result.message);
    if (result.state == ObsSocketReadState::closed)
    {
      if (const auto error = socket_.last_error(); !error.empty())
        set_error(error);
      return std::nullopt;
    }
    if (timeout != std::chrono::milliseconds::zero())
      return std::nullopt;
  }
  return std::nullopt;
}

bool ObsClient::send_message(std::string_view message)
{
  return socket_.send(std::string(message));
}
void ObsClient::trigger_hotkey(const ProductionHotkeyTrigger& trigger)
{
  const auto sequence = make_obs_hotkey_sequence(trigger.keys);
  request("TriggerHotkeyByKeySequence",
          {{"keyId", sequence.key_id},
           {"keyModifiers",
            {{"shift", sequence.shift},
             {"control", sequence.control},
             {"alt", sequence.alt},
             {"command", false}}}},
          std::source_location::current(),
          "trigger authenticated client hotkey");
}

std::optional<std::vector<ProductionHotkeyDefinition>> ObsClient::hotkeys()
{
  const auto bindings = quick_transition_control_.hotkey_catalog();
  if (!bindings)
    return std::nullopt;
  return map_obs_hotkey_catalog(*bindings);
}

json ObsClient::request(std::string_view type,
                        json data,
                        std::source_location caller,
                        std::string_view reason)
{
  if (reader_id_.load(std::memory_order_acquire) == GetCurrentThreadId())
    throw std::logic_error("OBS callbacks cannot issue synchronous requests");
  if (!connected())
    throw std::runtime_error("OBS is not connected");
  if (type == "GetPerformance")
    return obs_performance_snapshot(*this, caller);
  const auto transition = quick_transition_identities_.resolve(data);
  if (type == "SetQuickTransitionDuration")
  {
    if (!transition.native_id || !transition.duration_ms)
      throw std::runtime_error("OBS quick-transition configuration is incomplete");
    quick_transition_control_.configure_duration(*transition.native_id, *transition.duration_ms);
    return json::object();
  }
  if (type == "TriggerStudioModeTransition" && transition.native_id)
  {
    quick_transition_control_.trigger(*transition.native_id);
    return json::object();
  }
  if (type == "TriggerStudioModeTransition")
    prepare_obs_transition_fallback(*this, data, caller);
  return request_wire(type, std::move(data), caller, reason);
}

json ObsClient::request_wire(std::string_view type,
                             json data,
                             std::source_location caller,
                             std::string_view reason)
{
  if (pending_.full())
    throw std::runtime_error("OBS request queue is full");
  auto id = WindowsCrypto::uuid_v4();
  const auto request_started_ns = monotonic_ns();
  const ObsRpcRequestOperations operations{
      [this](std::string_view request_id)
      { return pending_.register_request(std::string(request_id)); },
      [this](std::string_view request_type, std::string_view request_id, json request_data)
      { return send_message(make_obs_request(request_type, request_id, std::move(request_data))); },
      [request_started_ns] { return (monotonic_ns() - request_started_ns) / 1'000'000ULL; }};
  return exchange_obs_rpc_request(
      pending_,
      {type, id, std::move(data), std::chrono::seconds(8), caller, reason, GetCurrentThreadId()},
      operations,
      &diagnostics_,
      [this, type](json& response) { map_response(type, response); });
}

void ObsClient::map_response(std::string_view type, json& response)
{
  if (type == "GetSceneTransitionList")
  {
    const auto websocket_catalog = response.value("transitions", json::array());
    const auto root = configuration_root_ ? *configuration_root_ : obs_configuration_root();
    auto configured_quick_transitions = load_obs_quick_transition_catalog(root, websocket_catalog);
    auto observed_quick_transitions = quick_transition_control_.catalog();
    auto quick_transitions =
        observed_quick_transitions
            ? map_obs_quick_transition_hotkey_catalog(
                  *observed_quick_transitions, websocket_catalog, configured_quick_transitions)
            : std::move(configured_quick_transitions);
    quick_transition_identities_.replace(quick_transitions);
    response["transitions"] = std::move(quick_transitions);
  }
}

void ObsClient::dispatch(std::string_view wire_message)
{
  auto message = parse_obs_message(wire_message, pending_);
  if (auto* event = std::get_if<ObsWireEvent>(&message))
  {
    diagnostics_.event_received(event->type, event->data, GetCurrentThreadId());
    emit_event({event->type, event->data, monotonic_ns()});
  }
  if (auto* response = std::get_if<ObsWireResponse>(&message))
    complete_obs_response(pending_, std::move(*response));
}

void ObsClient::emit_connection(bool connected, std::string_view version)
{
  if (const auto failure = callbacks_.connection_changed(connected, version))
    if (set_error(std::string(*failure)))
      OutputDebugStringA((std::string(*failure) + "\n").c_str());
}

void ObsClient::emit_event(const NormalizedObsEvent& event)
{
  if (const auto failure = callbacks_.event_received(event))
    if (set_error(std::string(*failure)))
      OutputDebugStringA((std::string(*failure) + "\n").c_str());
}

std::uint32_t ObsClient::reconnect_random_sample()
{
  std::uint32_t sample = 0;
  if (BCryptGenRandom(nullptr,
                      reinterpret_cast<PUCHAR>(&sample),
                      static_cast<ULONG>(sizeof(sample)),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
  {
    if (set_error("Could not generate OBS reconnect jitter"))
      OutputDebugStringA("Could not generate OBS reconnect jitter\n");
    return 0;
  }
  return sample;
}

void ObsClient::on_diagnostic_event(DiagnosticEventHandler handler)
{
  diagnostics_.on_event(std::move(handler));
}

void ObsClient::close_socket()
{
  socket_.close();
}
} // namespace studio_remote::windows
