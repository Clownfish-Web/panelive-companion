#include "macos_obs_client.hpp"

#include "macos_obs_quick_transition_catalog.hpp"
#include "macos_obs_websocket.hpp"

#include <pthread.h>
#include <studio_remote/obs/obs_hotkey_sequence.hpp>
#include <studio_remote/obs/obs_performance_snapshot.hpp>
#include <syslog.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
ObsConnection::SocketFactory socket_factory(ObsConnection::SocketFactory supplied)
{
  if (supplied)
    return supplied;
  return [] { return std::make_unique<MacosObsWebSocket>(); };
}

} // namespace

ObsClient::ObsClient(std::string host, std::uint16_t port, std::string password)
    : ObsClient(std::move(host), port, std::move(password), ObsClientInputs{})
{
}
ObsClient::ObsClient(std::string host,
                     std::uint16_t port,
                     std::string password,
                     ObsClientInputs inputs)
    : configuration_root_(std::move(inputs.configuration_root)),
      quick_transition_control_(std::move(inputs.transition_mapping)),
      capabilities_(std::move(inputs.capability_mapping)),
      connection_(
          std::move(host), port, std::move(password), socket_factory(std::move(inputs.sockets)))
{
  on_event({});
}
ObsClient::~ObsClient()
{
  stop();
}
void ObsClient::start()
{
  connection_.start();
}
void ObsClient::stop()
{
  connection_.stop();
}
void ObsClient::reconfigure(std::string host, std::uint16_t port, std::string password)
{
  connection_.reconfigure(std::move(host), port, std::move(password));
}
bool ObsClient::connected() const
{
  return connection_.connected();
}
std::string ObsClient::last_error() const
{
  return connection_.last_error();
}
void ObsClient::on_diagnostic_event(DiagnosticEventHandler callback)
{
  diagnostics_.on_event(std::move(callback));
}
void ObsClient::on_event(EventCallback callback)
{
  connection_.on_event(
      [this, callback = std::move(callback)](const NormalizedObsEvent& event)
      {
        std::uint64_t thread_id = 0;
        if (pthread_threadid_np(nullptr, &thread_id) != 0)
          syslog(LOG_ERR, "Could not read OBS diagnostic thread ID");
        diagnostics_.event_received(event.type, event.data, thread_id);
        if (callback)
          callback(event);
      });
}
void ObsClient::on_connection(ConnectionCallback callback)
{
  connection_.on_connection(std::move(callback));
}
ProductionBackendInfo ObsClient::backend_info() const
{
  ProductionBackendInfo info;
  info.capabilities.audio_input_positive_gain = true;
  info.capabilities.authoritative_control_verification = true;
  info.capabilities.explicit_start_stop_for_record = true;
  info.capabilities.explicit_start_stop_for_stream = true;
  info.capabilities.performance = true;
  return info;
}
bool ObsClient::recording_pause_available() const
{
  return capabilities_.recording_pause_available();
}
bool ObsClient::hotkey_catalog_supported() const noexcept
{
  return true;
}
std::optional<std::vector<ProductionHotkeyDefinition>> ObsClient::hotkeys()
{
  const auto observed = quick_transition_control_.hotkey_catalog();
  if (!observed)
    return std::nullopt;
  return map_obs_hotkey_catalog(*observed, ObsCommandKeySupport::available);
}
void ObsClient::trigger_hotkey(const ProductionHotkeyTrigger& trigger)
{
  const auto sequence = make_obs_hotkey_sequence(trigger.keys, ObsCommandKeySupport::available);
  request("TriggerHotkeyByKeySequence",
          {{"keyId", sequence.key_id},
           {"keyModifiers",
            {{"shift", sequence.shift},
             {"control", sequence.control},
             {"alt", sequence.alt},
             {"command", sequence.command}}}},
          std::source_location::current(),
          "trigger authenticated client hotkey");
}
json ObsClient::request(std::string_view type,
                        json data,
                        std::source_location caller,
                        std::string_view reason)
{
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
  return connection_.request(type,
                             std::move(data),
                             &diagnostics_,
                             caller,
                             reason,
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
} // namespace studio_remote::macos
