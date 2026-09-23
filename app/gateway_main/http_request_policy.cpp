#include "http_request_policy.hpp"
#include "tally_publication_routes.hpp"
#include "teleprompter_protocol.hpp"
#include "teleprompter_session_protocol.hpp"

#include <algorithm>
#include <array>

namespace studio_remote
{
namespace
{
RequestScope get_scope(std::string_view path)
{
  if (path == "/v1/identity")
    return RequestScope::identity;
  if (path == "/v1/admin/diagnostics")
    return RequestScope::administration;
  if (teleprompter_read_path(path) || teleprompter_session_read_path(path))
    return RequestScope::teleprompter;
  if ((path.starts_with("/v1/resources/") || path.starts_with("/v1/compositions/")) &&
      path.ends_with("/thumbnail"))
    return RequestScope::thumbnail;
  if (path == "/v1/state" || path == "/v1/capabilities" || path == "/v1/performance" ||
      path == "/v1/critical-queue" || path.starts_with("/v1/critical-actions/"))
    return RequestScope::state;
  constexpr std::array catalogs = {"/v1/compositions",
                                   "/v1/transitions",
                                   "/v1/canvases",
                                   "/v1/scene-collections",
                                   "/v1/obs/profiles",
                                   "/v1/obs/scene-collections",
                                   "/v1/streaming-destinations",
                                   "/v1/hotkeys",
                                   "/v1/audio-inputs",
                                   "/v1/telestrator"};
  if (std::find(catalogs.begin(), catalogs.end(), path) != catalogs.end() ||
      (path.starts_with("/v1/compositions/") && path.ends_with("/layers")))
    return RequestScope::catalog;
  return RequestScope::invalid_request;
}

RequestScope post_scope(std::string_view path)
{
  if (teleprompter_progress_path(path) || teleprompter_session_command_path(path) ||
      teleprompter_session_position_path(path))
    return RequestScope::teleprompter;
  if (path == "/v1/pairing/exchange")
    return RequestScope::pairing;
  if (path == "/v1/critical-actions")
    return RequestScope::critical_control;
  if (path == "/v1/critical-controls/acknowledge-block")
    return RequestScope::state;
  if (path == "/v1/hotkeys/trigger")
    return RequestScope::hotkey;
  if (path == "/v1/webrtc/offer")
    return RequestScope::media_offer;
  if (path == "/v1/webrtc/recovery")
    return RequestScope::media_recovery;
  if (path.starts_with("/v1/admin/"))
    return RequestScope::administration;
  if (path == "/v1/role-change-requests" || path == "/v1/device/rename")
    return RequestScope::device_management;
  if (path == "/v1/commands" || path.starts_with("/v1/transitions/") ||
      path == "/v1/scene-collections/current" || path == "/v1/obs/profiles/current" ||
      path == "/v1/obs/scene-collections/current")
    return RequestScope::control;
  return RequestScope::invalid_request;
}
} // namespace

RequestScope http_request_scope(HttpRequestMethod method, std::string_view path)
{
  const auto tally = tally_route(path);
  if ((method == HttpRequestMethod::get &&
       (tally.kind == TallyRouteKind::collection || tally.kind == TallyRouteKind::publication)) ||
      (method == HttpRequestMethod::post && tally.kind == TallyRouteKind::rotate) ||
      ((method == HttpRequestMethod::put || method == HttpRequestMethod::delete_) &&
       tally.kind == TallyRouteKind::publication))
    return RequestScope::remote_tally;
  if (method == HttpRequestMethod::get)
    return get_scope(path);
  if (method == HttpRequestMethod::post)
    return post_scope(path);
  return RequestScope::invalid_request;
}
} // namespace studio_remote
