#pragma once

#include "../../core/application/obs_configuration_service.hpp"

#include <optional>

namespace studio_remote
{

struct ObsConfigurationHttpOutcome
{
  int status = 500;
  json body;
};

std::optional<ObsProfileSelection> parse_obs_profile_selection(const json& body);
std::optional<ObsSceneCollectionSelection> parse_obs_scene_collection_selection(const json& body);

ObsConfigurationHttpOutcome profile_catalog_response(const ObsProfileCatalogResult& result);
ObsConfigurationHttpOutcome
scene_collection_catalog_response(const ObsSceneCollectionCatalogResult& result);
ObsConfigurationHttpOutcome
profile_selection_response(const ObsConfigurationSelectionOutcome& result);
ObsConfigurationHttpOutcome
scene_collection_selection_response(const ObsConfigurationSelectionOutcome& result);

} // namespace studio_remote
