#include <studio_remote/obs/obs_diagnostic_events.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <unordered_set>
#include <utility>

namespace studio_remote
{
namespace
{
std::string lowercase(std::string_view value)
{
  std::string result(value);
  std::transform(result.begin(),
                 result.end(),
                 result.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return result;
}

bool sensitive_diagnostic_key(std::string_view key)
{
  const auto normalized = lowercase(key);
  constexpr std::array sensitive_fragments{"authorization",
                                           "cookie",
                                           "local_file",
                                           "password",
                                           "path",
                                           "privatekey",
                                           "secret",
                                           "streamkey",
                                           "token"};
  return std::any_of(sensitive_fragments.begin(),
                     sensitive_fragments.end(),
                     [&](std::string_view fragment)
                     { return normalized.find(fragment) != std::string::npos; });
}

json sanitized_diagnostic_value(const json& value)
{
  if (value.is_object())
  {
    json result = json::object();
    for (const auto& [key, child] : value.items())
      result[key] =
          sensitive_diagnostic_key(key) ? json("[redacted]") : sanitized_diagnostic_value(child);
    return result;
  }
  if (value.is_array())
  {
    json result = json::array();
    for (const auto& child : value)
      result.push_back(sanitized_diagnostic_value(child));
    return result;
  }
  return value;
}

std::string request_subsystem(const std::source_location& caller)
{
  const auto file = lowercase(caller.file_name());
  if (file.find("obs_state_reconciler") != std::string::npos)
    return "state-reconciliation";
  if (file.find("gateway_command_processor") != std::string::npos)
    return "client-command";
  if (file.find("critical_live_control_service") != std::string::npos)
    return "critical-live-control";
  if (file.find("diagnostics_cli") != std::string::npos)
    return "diagnostics-cli";
  return "obs-adapter-caller";
}

std::string request_reason(std::string_view request_type, std::string_view explicit_reason)
{
  if (!explicit_reason.empty())
    return std::string(explicit_reason);
  return request_type.starts_with("Get") ? "read OBS state without mutation"
                                         : "execute an authorized OBS control";
}

bool important_diagnostic_event(std::string_view type)
{
  static const std::unordered_set<std::string_view> event_types{
      "CurrentProgramSceneChanged",
      "CurrentPreviewSceneChanged",
      "CurrentSceneTransitionChanged",
      "CurrentSceneTransitionDurationChanged",
      "SceneTransitionStarted",
      "SceneTransitionEnded",
      "SceneItemEnableStateChanged",
      "InputActiveStateChanged",
      "InputShowStateChanged",
      "MediaInputPlaybackStarted",
      "MediaInputPlaybackEnded",
      "MediaInputActionTriggered",
  };
  return event_types.contains(type);
}
} // namespace

void ObsDiagnosticEvents::on_event(Handler handler)
{
  std::lock_guard lock(mutex_);
  handler_ = std::move(handler);
}

void ObsDiagnosticEvents::emit(std::string_view event, json details) const
{
  Handler handler;
  {
    std::lock_guard lock(mutex_);
    handler = handler_;
  }
  if (!handler)
    return;
  try
  {
    handler(event, std::move(details));
  }
  catch (...)
  {
    // Preserve Windows policy: a failing diagnostic sink must not change OBS delivery.
  }
}

void ObsDiagnosticEvents::request_sent(std::string_view type,
                                       std::string_view id,
                                       const json& data,
                                       std::source_location caller,
                                       std::string_view reason,
                                       std::uint64_t thread_id) const
{
  emit("requestSent",
       {{"requestType", type},
        {"requestId", id},
        {"arguments", sanitized_diagnostic_value(data)},
        {"callerFile", std::filesystem::path(caller.file_name()).filename().string()},
        {"callerFunction", caller.function_name()},
        {"callerLine", caller.line()},
        {"subsystem", request_subsystem(caller)},
        {"threadId", thread_id},
        {"reason", request_reason(type, reason)}});
}

void ObsDiagnosticEvents::request_send_failed(std::string_view type,
                                              std::string_view id,
                                              std::uint64_t thread_id) const
{
  emit("requestSendFailed", {{"requestType", type}, {"requestId", id}, {"threadId", thread_id}});
}

void ObsDiagnosticEvents::request_timed_out(std::string_view type,
                                            std::string_view id,
                                            std::uint64_t elapsed_ms,
                                            std::uint64_t thread_id) const
{
  emit("requestTimedOut",
       {{"requestType", type},
        {"requestId", id},
        {"elapsedMs", elapsed_ms},
        {"threadId", thread_id}});
}

void ObsDiagnosticEvents::request_completed(std::string_view type,
                                            std::string_view id,
                                            std::uint64_t elapsed_ms,
                                            std::uint64_t thread_id) const
{
  emit("requestCompleted",
       {{"requestType", type},
        {"requestId", id},
        {"elapsedMs", elapsed_ms},
        {"threadId", thread_id}});
}

void ObsDiagnosticEvents::request_rejected(std::string_view type,
                                           std::string_view id,
                                           std::uint64_t elapsed_ms,
                                           std::uint64_t thread_id) const
{
  emit("requestRejected",
       {{"requestType", type},
        {"requestId", id},
        {"elapsedMs", elapsed_ms},
        {"threadId", thread_id},
        {"error", "obs_request_rejected"}});
}

void ObsDiagnosticEvents::event_received(std::string_view type,
                                         const json& data,
                                         std::uint64_t thread_id) const
{
  if (important_diagnostic_event(type))
    emit("eventReceived",
         {{"eventType", type},
          {"eventData", sanitized_diagnostic_value(data)},
          {"threadId", thread_id}});
}
} // namespace studio_remote
