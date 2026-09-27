#include "linux_obs_client.hpp"

#include "../credentials/linux_crypto.hpp"
#include "linux_obs_hotkey_catalog.hpp"
#include "linux_obs_quick_transition_catalog.hpp"

#include <algorithm>
#include <studio_remote/obs/obs_rpc_request_exchange.hpp>
#include <studio_remote/obs/obs_wire_protocol.hpp>
#include <sys/syscall.h>
#include <unistd.h>

namespace studio_remote::linux
{
namespace
{
std::string obs_key_id(const HotkeyKey& key)
{
  if (key.kind == HotkeyKeyKind::letter || key.kind == HotkeyKeyKind::digit ||
      key.kind == HotkeyKeyKind::function)
    return "OBS_KEY_" + hotkey_key_name(key);
  switch (key.named)
  {
  case HotkeyNamedKey::escape:
    return "OBS_KEY_ESCAPE";
  case HotkeyNamedKey::tab:
    return "OBS_KEY_TAB";
  case HotkeyNamedKey::enter:
    return "OBS_KEY_RETURN";
  case HotkeyNamedKey::space:
    return "OBS_KEY_SPACE";
  case HotkeyNamedKey::backspace:
    return "OBS_KEY_BACKSPACE";
  case HotkeyNamedKey::delete_key:
    return "OBS_KEY_DELETE";
  case HotkeyNamedKey::home:
    return "OBS_KEY_HOME";
  case HotkeyNamedKey::end:
    return "OBS_KEY_END";
  case HotkeyNamedKey::left:
    return "OBS_KEY_LEFT";
  case HotkeyNamedKey::right:
    return "OBS_KEY_RIGHT";
  case HotkeyNamedKey::up:
    return "OBS_KEY_UP";
  case HotkeyNamedKey::down:
    return "OBS_KEY_DOWN";
  case HotkeyNamedKey::comma:
    return "OBS_KEY_COMMA";
  case HotkeyNamedKey::minus:
    return "OBS_KEY_MINUS";
  case HotkeyNamedKey::period:
    return "OBS_KEY_PERIOD";
  case HotkeyNamedKey::slash:
    return "OBS_KEY_SLASH";
  default:
    throw HotkeyExecutionError("production-hotkey-key-unsupported",
                               "OBS does not support this key in a key-sequence request");
  }
}
} // namespace

ObsClient::ObsClient(std::string host, std::uint16_t port, std::string password)
    : host_(std::move(host)), password_(std::move(password)), port_(port)
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
  stopping_ = false;
  thread_ = std::thread([this] { run(); });
}

void ObsClient::stop()
{
  stopping_ = true;
  close_socket();
  socket_cv_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void ObsClient::reconfigure(std::string host, std::uint16_t port, std::string password)
{
  {
    std::lock_guard lock(mutex_);
    host_ = std::move(host);
    port_ = port;
    password_ = std::move(password);
    configuration_revision_.fetch_add(1, std::memory_order_release);
  }
  set_error("OBS connection settings changed; reconnecting");
  close_socket();
}

bool ObsClient::connected() const
{
  return connected_.load();
}

std::string ObsClient::last_error() const
{
  std::lock_guard lock(mutex_);
  return last_error_;
}

ProductionBackendInfo ObsClient::backend_info() const
{
  ProductionBackendInfo result;
  result.capabilities.audio_input_positive_gain = true;
  result.capabilities.authoritative_control_verification = true;
  result.capabilities.explicit_start_stop_for_record = true;
  result.capabilities.explicit_start_stop_for_stream = true;
  result.capabilities.performance = true;
  return result;
}

bool ObsClient::hotkey_catalog_supported() const noexcept
{
  return true;
}

bool ObsClient::recording_pause_available() const
{
  return runtime_capabilities_.recording_pause_available();
}

void ObsClient::on_event(EventCallback handler)
{
  std::lock_guard lock(mutex_);
  event_handler_ = std::move(handler);
}

void ObsClient::on_connection(ConnectionCallback handler)
{
  std::lock_guard lock(mutex_);
  connection_handler_ = std::move(handler);
}

void ObsClient::on_diagnostic_event(DiagnosticEventHandler handler)
{
  diagnostics_.on_event(std::move(handler));
}

void ObsClient::run()
{
  auto backoff = std::chrono::seconds(1);
  while (!stopping_)
  {
    const auto revision = configuration_revision_.load(std::memory_order_acquire);
    try
    {
      if (connect_once(revision))
      {
        backoff = std::chrono::seconds(1);
        while (!stopping_)
        {
          const auto message = receive_message(std::chrono::seconds(30));
          if (!message)
          {
            std::lock_guard lock(mutex_);
            if (socket_closed_ || stopping_)
              break;
            continue;
          }
          dispatch(*message);
        }
      }
    }
    catch (const std::exception& error)
    {
      set_error(error.what());
    }
    const bool was_connected = connected_.exchange(false);
    close_socket();
    pending_.cancel_all(std::make_exception_ptr(std::runtime_error("OBS connection lost")));
    ConnectionCallback connection_handler;
    {
      std::lock_guard lock(mutex_);
      connection_handler = connection_handler_;
    }
    if (was_connected && connection_handler)
      connection_handler(false, {});
    std::unique_lock lock(mutex_);
    const auto configuration_changed = socket_cv_.wait_for(
        lock,
        backoff,
        [this, revision] {
          return stopping_ || configuration_revision_.load(std::memory_order_acquire) != revision;
        });
    backoff = configuration_changed ? std::chrono::seconds(1)
                                    : std::min(backoff * 2, std::chrono::seconds(30));
  }
}

bool ObsClient::connect_once(std::uint64_t configuration_revision)
{
  std::string host;
  std::string password;
  std::uint16_t port = 0;
  {
    std::lock_guard lock(mutex_);
    host = host_;
    password = password_;
    port = port_;
    messages_.clear();
    socket_open_ = false;
    socket_closed_ = false;
  }
  rtc::WebSocket::Configuration configuration;
  configuration.connectionTimeout = std::chrono::seconds(5);
  configuration.pingInterval = std::chrono::seconds(15);
  configuration.maxMessageSize = 16 * 1024 * 1024;
  auto socket = std::make_shared<rtc::WebSocket>(configuration);
  socket->onOpen(
      [this]
      {
        std::lock_guard lock(mutex_);
        socket_open_ = true;
        socket_cv_.notify_all();
      });
  socket->onClosed(
      [this]
      {
        std::lock_guard lock(mutex_);
        socket_closed_ = true;
        socket_cv_.notify_all();
      });
  socket->onError(
      [this](std::string error)
      {
        set_error("OBS WebSocket: " + error);
        std::lock_guard lock(mutex_);
        socket_closed_ = true;
        socket_cv_.notify_all();
      });
  socket->onMessage(
      [this](rtc::message_variant value)
      {
        if (!std::holds_alternative<std::string>(value))
          return;
        std::lock_guard lock(mutex_);
        messages_.push_back(std::get<std::string>(std::move(value)));
        socket_cv_.notify_all();
      });
  {
    std::lock_guard lock(mutex_);
    socket_ = socket;
  }
  socket->open("ws://" + host + ":" + std::to_string(port) + "/");
  {
    std::unique_lock lock(mutex_);
    if (!socket_cv_.wait_for(lock,
                             std::chrono::seconds(6),
                             [this] { return socket_open_ || socket_closed_ || stopping_; }) ||
        !socket_open_)
      return false;
  }
  const auto hello_text = receive_message(std::chrono::seconds(6));
  if (!hello_text)
    throw std::runtime_error("OBS closed before Hello");
  const auto hello = parse_obs_hello(*hello_text, password);
  if (!send_message(hello.identify_message))
    throw std::runtime_error("Could not send OBS Identify");
  const auto identified_text = receive_message(std::chrono::seconds(6));
  if (!identified_text)
    throw std::runtime_error("OBS rejected or closed during Identify");
  validate_obs_identified(*identified_text);
  if (configuration_revision_.load(std::memory_order_acquire) != configuration_revision)
    return false;
  connected_ = true;
  set_error({});
  ConnectionCallback connection_handler;
  {
    std::lock_guard lock(mutex_);
    connection_handler = connection_handler_;
  }
  if (connection_handler)
    connection_handler(true, hello.version);
  return true;
}

std::optional<std::string> ObsClient::receive_message(std::chrono::milliseconds timeout)
{
  std::unique_lock lock(mutex_);
  if (!socket_cv_.wait_for(
          lock, timeout, [this] { return !messages_.empty() || socket_closed_ || stopping_; }))
    return std::nullopt;
  if (messages_.empty())
    return std::nullopt;
  auto message = std::move(messages_.front());
  messages_.pop_front();
  return message;
}

bool ObsClient::send_message(std::string message)
{
  std::shared_ptr<rtc::WebSocket> socket;
  {
    std::lock_guard lock(mutex_);
    socket = socket_;
  }
  return socket && socket->isOpen() && socket->send(std::move(message));
}

json ObsClient::request(std::string_view type,
                        json data,
                        std::source_location caller,
                        std::string_view reason)
{
  if (!connected_)
    throw std::runtime_error("OBS is not connected");
  if (type == "GetPerformance")
  {
    const auto stats = request("GetStats", {}, caller, "sample OBS performance statistics");
    const auto stream = request("GetStreamStatus", {}, caller, "sample OBS stream statistics");
    json result = stats;
    if (stream.contains("outputCongestion"))
      result["streamCongestionRatio"] = stream["outputCongestion"];
    return result;
  }
  const bool configure_quick_transition = type == "SetQuickTransitionDuration";
  std::optional<std::int32_t> native_quick_transition_id;
  std::optional<std::uint32_t> requested_quick_transition_duration;
  const auto quick_transition_id = data.find("quickTransitionId");
  if (quick_transition_id != data.end() && quick_transition_id->is_string())
  {
    const auto transition_id = quick_transition_id->get<std::string>();
    std::lock_guard lock(quick_transition_mutex_);
    if (const auto backend_name = quick_transition_backend_names_.find(transition_id);
        backend_name != quick_transition_backend_names_.end() && data.contains("transitionName"))
      data["transitionName"] = backend_name->second;
    if (const auto native_id = quick_transition_native_ids_.find(transition_id);
        native_id != quick_transition_native_ids_.end())
      native_quick_transition_id = native_id->second;
    const auto duration = data.find("transitionDuration");
    if (duration != data.end() && duration->is_number_unsigned())
      requested_quick_transition_duration = duration->get<std::uint32_t>();
    data.erase(quick_transition_id);
  }
  if (configure_quick_transition)
  {
    if (!native_quick_transition_id || !requested_quick_transition_duration)
      throw std::runtime_error("OBS quick-transition configuration is incomplete");
    quick_transition_control_.configure_duration(*native_quick_transition_id,
                                                 *requested_quick_transition_duration);
    return json::object();
  }
  if (type == "TriggerStudioModeTransition" && native_quick_transition_id)
  {
    quick_transition_control_.trigger(*native_quick_transition_id);
    return json::object();
  }
  if (type == "TriggerStudioModeTransition")
  {
    const auto duration = data.find("transitionDuration");
    const auto transition_name = data.find("transitionName");
    if (transition_name != data.end() && transition_name->is_string() &&
        !transition_name->get_ref<const std::string&>().empty())
    {
      request("SetCurrentSceneTransition",
              {{"transitionName", *transition_name}},
              caller,
              "select requested quick transition");
      if (duration != data.end() && duration->is_number_unsigned())
        request("SetCurrentSceneTransitionDuration",
                {{"transitionDuration", *duration}},
                caller,
                "apply requested quick transition duration");
      data = json::object();
    }
  }
  if (pending_.full())
    throw std::runtime_error("OBS request queue is full");
  const auto request_id = crypto::uuid_v4();
  const auto started = monotonic_ns();
  const ObsRpcRequestOperations operations{
      [this](std::string_view id) { return pending_.register_request(std::string(id)); },
      [this](std::string_view request_type, std::string_view id, json request_data)
      { return send_message(make_obs_request(request_type, id, std::move(request_data))); },
      [started] { return (monotonic_ns() - started) / 1'000'000ULL; }};
  auto result = exchange_obs_rpc_request(pending_,
                                         {type,
                                          request_id,
                                          std::move(data),
                                          std::chrono::seconds(8),
                                          caller,
                                          reason,
                                          static_cast<std::uint64_t>(syscall(SYS_gettid))},
                                         operations,
                                         &diagnostics_);
  if (type == "GetSceneTransitionList")
  {
    const auto websocket_catalog = result.value("transitions", json::array());
    auto configured =
        load_obs_quick_transition_catalog(obs_configuration_root(), websocket_catalog);
    const auto observed = quick_transition_control_.catalog();
    auto transitions =
        observed ? map_obs_quick_transition_hotkey_catalog(*observed, websocket_catalog, configured)
                 : std::move(configured);
    {
      std::lock_guard lock(quick_transition_mutex_);
      quick_transition_backend_names_.clear();
      quick_transition_native_ids_.clear();
      for (auto& transition : transitions)
      {
        const auto id = transition.value("quickTransitionId", "");
        const auto backend_name = transition.value("backendTransitionName", "");
        if (!id.empty() && !backend_name.empty())
          quick_transition_backend_names_[id] = backend_name;
        if (!id.empty() && transition.value("quickTransitionNativeId", 0) > 0)
          quick_transition_native_ids_[id] = transition.value("quickTransitionNativeId", 0);
        transition.erase("backendTransitionName");
        transition.erase("quickTransitionNativeId");
      }
    }
    result["transitions"] = std::move(transitions);
  }
  return result;
}

void ObsClient::dispatch(std::string_view wire_message)
{
  auto message = parse_obs_message(wire_message, pending_);
  if (auto* event = std::get_if<ObsWireEvent>(&message))
  {
    diagnostics_.event_received(
        event->type, event->data, static_cast<std::uint64_t>(syscall(SYS_gettid)));
    EventCallback event_handler;
    {
      std::lock_guard lock(mutex_);
      event_handler = event_handler_;
    }
    if (event_handler)
      event_handler({event->type, event->data, monotonic_ns()});
  }
  if (auto* response = std::get_if<ObsWireResponse>(&message))
    complete_obs_response(pending_, std::move(*response));
}

void ObsClient::trigger_hotkey(const ProductionHotkeyTrigger& trigger)
{
  json modifiers = {{"shift", false}, {"control", false}, {"alt", false}, {"command", false}};
  std::optional<std::string> key_id;
  for (const auto& key : trigger.keys)
  {
    if (key.kind == HotkeyKeyKind::named)
    {
      if (key.named == HotkeyNamedKey::control || key.named == HotkeyNamedKey::left_control ||
          key.named == HotkeyNamedKey::right_control)
      {
        modifiers["control"] = true;
        continue;
      }
      if (key.named == HotkeyNamedKey::shift || key.named == HotkeyNamedKey::left_shift ||
          key.named == HotkeyNamedKey::right_shift)
      {
        modifiers["shift"] = true;
        continue;
      }
      if (key.named == HotkeyNamedKey::alt || key.named == HotkeyNamedKey::left_alt ||
          key.named == HotkeyNamedKey::right_alt)
      {
        modifiers["alt"] = true;
        continue;
      }
    }
    if (key_id)
      throw HotkeyExecutionError("production-hotkey-chord-unsupported",
                                 "OBS accepts one key with optional modifiers");
    key_id = obs_key_id(key);
  }
  if (!key_id)
    throw HotkeyExecutionError("production-hotkey-key-required",
                               "OBS requires one non-modifier key");
  request("TriggerHotkeyByKeySequence",
          {{"keyId", *key_id}, {"keyModifiers", modifiers}},
          std::source_location::current(),
          "trigger authenticated client hotkey");
}

std::optional<std::vector<ProductionHotkeyDefinition>> ObsClient::hotkeys()
{
  const auto bindings = quick_transition_control_.hotkey_catalog();
  return bindings ? std::optional{map_obs_hotkey_catalog(*bindings)} : std::nullopt;
}

void ObsClient::close_socket()
{
  std::shared_ptr<rtc::WebSocket> socket;
  {
    std::lock_guard lock(mutex_);
    socket = std::move(socket_);
    socket_closed_ = true;
    socket_open_ = false;
    socket_cv_.notify_all();
  }
  if (socket)
  {
    socket->resetCallbacks();
    socket->forceClose();
  }
}

void ObsClient::set_error(std::string message)
{
  std::lock_guard lock(mutex_);
  last_error_ = std::move(message);
}

} // namespace studio_remote::linux
