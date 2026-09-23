#include "obs_configuration_protocol.hpp"

#include <algorithm>

namespace studio_remote
{
namespace
{
constexpr std::size_t maximum_configuration_name_bytes = 128;

bool valid_name(std::string_view name)
{
  if (name.empty() || name.size() > maximum_configuration_name_bytes || name.front() == ' ' ||
      name.back() == ' ' || name.front() == '\t' || name.back() == '\t' || name.front() == '\r' ||
      name.back() == '\r' || name.front() == '\n' || name.back() == '\n')
    return false;
  return std::none_of(name.begin(),
                      name.end(),
                      [](unsigned char character)
                      { return character < 0x20U || character == 0x7FU; });
}

std::optional<std::string> required_name(const json& body, std::string_view field)
{
  const auto value = body.find(std::string(field));
  if (value == body.end() || !value->is_string())
    return std::nullopt;
  auto name = value->get<std::string>();
  return valid_name(name) ? std::optional<std::string>(std::move(name)) : std::nullopt;
}

ObsConfigurationHttpOutcome failure_response(const ObsConfigurationFailure& failure, bool profile)
{
  std::string error;
  int status = 503;
  switch (failure.code)
  {
  case ObsConfigurationFailureCode::operator_role_required:
    status = 403;
    error = "operator-role-required";
    break;
  case ObsConfigurationFailureCode::unsupported:
    status = 404;
    error = "obs-configuration-unsupported";
    break;
  case ObsConfigurationFailureCode::backend_unavailable:
    error = "production-backend-unavailable";
    break;
  case ObsConfigurationFailureCode::profile_precondition_failed:
    status = 409;
    error = "obs-profile-precondition-failed";
    break;
  case ObsConfigurationFailureCode::scene_collection_precondition_failed:
    status = 409;
    error = "obs-scene-collection-precondition-failed";
    break;
  case ObsConfigurationFailureCode::profile_not_found:
    status = 409;
    error = "obs-profile-not-found";
    break;
  case ObsConfigurationFailureCode::scene_collection_not_found:
    status = 409;
    error = "obs-scene-collection-not-found";
    break;
  case ObsConfigurationFailureCode::live_output_active:
    status = 409;
    error = "obs-configuration-change-blocked-by-live-output";
    break;
  case ObsConfigurationFailureCode::verification_failed:
    status = 502;
    error = "obs-configuration-verification-failed";
    break;
  case ObsConfigurationFailureCode::configuration_unavailable:
    error = "obs-configuration-unavailable";
    break;
  }

  json body = {{"error", std::move(error)}};
  if (!failure.current_name.empty())
  {
    body[profile ? "currentProfileName" : "currentSceneCollectionName"] = failure.current_name;
  }
  return {status, std::move(body)};
}

json named_entries(const std::vector<std::string>& names, std::string_view current_name)
{
  json result = json::array();
  for (const auto& name : names)
    result.push_back({{"name", name}, {"current", name == current_name}});
  return result;
}
} // namespace

std::optional<ObsProfileSelection> parse_obs_profile_selection(const json& body)
{
  if (!body.is_object() || body.size() != 2 || !body.contains("profileName") ||
      !body.contains("expectedCurrentProfileName"))
    return std::nullopt;
  auto target = required_name(body, "profileName");
  auto expected = required_name(body, "expectedCurrentProfileName");
  if (!target || !expected)
    return std::nullopt;
  return ObsProfileSelection{std::move(*target), std::move(*expected)};
}

std::optional<ObsSceneCollectionSelection> parse_obs_scene_collection_selection(const json& body)
{
  if (!body.is_object() || body.size() != 2 || !body.contains("sceneCollectionName") ||
      !body.contains("expectedCurrentSceneCollectionName"))
    return std::nullopt;
  auto target = required_name(body, "sceneCollectionName");
  auto expected = required_name(body, "expectedCurrentSceneCollectionName");
  if (!target || !expected)
    return std::nullopt;
  return ObsSceneCollectionSelection{std::move(*target), std::move(*expected)};
}

ObsConfigurationHttpOutcome profile_catalog_response(const ObsProfileCatalogResult& result)
{
  if (const auto* failure = std::get_if<ObsConfigurationFailure>(&result))
    return failure_response(*failure, true);
  const auto& catalog = std::get<ObsProfileCatalog>(result);
  return {200,
          {{"type", "obsProfileList"},
           {"productionProvider", "obs"},
           {"currentProfileName", catalog.current_name},
           {"profiles", named_entries(catalog.names, catalog.current_name)}}};
}

ObsConfigurationHttpOutcome
scene_collection_catalog_response(const ObsSceneCollectionCatalogResult& result)
{
  if (const auto* failure = std::get_if<ObsConfigurationFailure>(&result))
    return failure_response(*failure, false);
  const auto& catalog = std::get<ObsSceneCollectionCatalog>(result);
  return {200,
          {{"type", "obsSceneCollectionList"},
           {"productionProvider", "obs"},
           {"currentSceneCollectionName", catalog.current_name},
           {"sceneCollections", named_entries(catalog.names, catalog.current_name)}}};
}

ObsConfigurationHttpOutcome
profile_selection_response(const ObsConfigurationSelectionOutcome& result)
{
  if (const auto* failure = std::get_if<ObsConfigurationFailure>(&result))
    return failure_response(*failure, true);
  const auto& selection = std::get<ObsConfigurationSelectionResult>(result);
  return {200,
          {{"type", "obsProfileSelectionResult"},
           {"status", "completed"},
           {"changed", selection.changed},
           {"previousProfileName", selection.previous_name},
           {"currentProfileName", selection.current_name}}};
}

ObsConfigurationHttpOutcome
scene_collection_selection_response(const ObsConfigurationSelectionOutcome& result)
{
  if (const auto* failure = std::get_if<ObsConfigurationFailure>(&result))
    return failure_response(*failure, false);
  const auto& selection = std::get<ObsConfigurationSelectionResult>(result);
  return {200,
          {{"type", "obsSceneCollectionSelectionResult"},
           {"status", "completed"},
           {"changed", selection.changed},
           {"previousSceneCollectionName", selection.previous_name},
           {"currentSceneCollectionName", selection.current_name}}};
}

} // namespace studio_remote
