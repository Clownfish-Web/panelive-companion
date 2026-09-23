#include "obs_configuration_service.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace studio_remote
{
namespace
{
constexpr std::size_t maximum_configuration_name_bytes = 128;
constexpr std::size_t maximum_catalog_entries = 256;

bool valid_configuration_name(std::string_view name)
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

template <typename Catalog>
Catalog
parse_catalog(const json& response, std::string_view current_field, std::string_view list_field)
{
  if (!response.is_object())
    throw std::runtime_error("OBS configuration catalog is not an object");

  const auto current = response.find(std::string(current_field));
  const auto entries = response.find(std::string(list_field));
  if (current == response.end() || !current->is_string() || entries == response.end() ||
      !entries->is_array() || entries->empty() || entries->size() > maximum_catalog_entries)
    throw std::runtime_error("OBS configuration catalog has an invalid shape");

  Catalog catalog;
  catalog.current_name = current->get<std::string>();
  if (!valid_configuration_name(catalog.current_name))
    throw std::runtime_error("OBS configuration catalog has an invalid current name");

  std::unordered_set<std::string> unique_names;
  catalog.names.reserve(entries->size());
  for (const auto& entry : *entries)
  {
    if (!entry.is_string())
      throw std::runtime_error("OBS configuration catalog entry is not a string");
    auto value = entry.get<std::string>();
    if (!valid_configuration_name(value) || !unique_names.insert(value).second)
      throw std::runtime_error("OBS configuration catalog entry name is invalid");
    catalog.names.push_back(std::move(value));
  }

  if (!unique_names.contains(catalog.current_name))
    throw std::runtime_error("OBS configuration catalog omits its current name");
  return catalog;
}

bool contains_name(const std::vector<std::string>& names, std::string_view target)
{
  return std::find(names.begin(), names.end(), target) != names.end();
}

bool required_boolean(const json& response, std::string_view field)
{
  const auto value = response.find(std::string(field));
  if (value == response.end() || !value->is_boolean())
    throw std::runtime_error("OBS output status has an invalid shape");
  return value->get<bool>();
}
} // namespace

ObsConfigurationService::ObsConfigurationService(IObsAdapter& obs,
                                                 AuditStore& audit,
                                                 ReconcileRequest request_reconcile)
    : obs_(obs), audit_(audit), request_reconcile_(std::move(request_reconcile))
{
}

std::optional<ObsConfigurationFailure>
ObsConfigurationService::authorize(const Device& device) const
{
  if (device.role < Role::operator_)
    return ObsConfigurationFailure{ObsConfigurationFailureCode::operator_role_required};
  if (device.production_provider != "obs" || obs_.backend_info().id != "obs")
    return ObsConfigurationFailure{ObsConfigurationFailureCode::unsupported};
  if (!obs_.connected())
    return ObsConfigurationFailure{ObsConfigurationFailureCode::backend_unavailable};
  return std::nullopt;
}

ObsProfileCatalog ObsConfigurationService::read_profiles()
{
  return parse_catalog<ObsProfileCatalog>(obs_.request("GetProfileList",
                                                       {},
                                                       std::source_location::current(),
                                                       "list OBS profiles for the paired client"),
                                          "currentProfileName",
                                          "profiles");
}

ObsSceneCollectionCatalog ObsConfigurationService::read_scene_collections()
{
  return parse_catalog<ObsSceneCollectionCatalog>(
      obs_.request("GetSceneCollectionList",
                   {},
                   std::source_location::current(),
                   "list OBS scene collections for the paired client"),
      "currentSceneCollectionName",
      "sceneCollections");
}

bool ObsConfigurationService::live_output_active()
{
  const auto stream = obs_.request("GetStreamStatus",
                                   {},
                                   std::source_location::current(),
                                   "verify streaming is stopped before changing OBS configuration");
  const auto record = obs_.request("GetRecordStatus",
                                   {},
                                   std::source_location::current(),
                                   "verify recording is stopped before changing OBS configuration");
  return required_boolean(stream, "outputActive") || required_boolean(record, "outputActive") ||
         required_boolean(record, "outputPaused");
}

ObsProfileCatalogResult ObsConfigurationService::profiles(const Device& device)
{
  if (const auto failure = authorize(device))
    return *failure;
  std::lock_guard operation_lock(operation_mutex_);
  try
  {
    return read_profiles();
  }
  catch (...)
  {
    return ObsConfigurationFailure{ObsConfigurationFailureCode::configuration_unavailable};
  }
}

ObsSceneCollectionCatalogResult ObsConfigurationService::scene_collections(const Device& device)
{
  if (const auto failure = authorize(device))
    return *failure;
  std::lock_guard operation_lock(operation_mutex_);
  try
  {
    return read_scene_collections();
  }
  catch (...)
  {
    return ObsConfigurationFailure{ObsConfigurationFailureCode::configuration_unavailable};
  }
}

ObsConfigurationSelectionOutcome
ObsConfigurationService::select_profile(const Device& device, const ObsProfileSelection& selection)
{
  if (const auto failure = authorize(device))
    return *failure;
  std::lock_guard operation_lock(operation_mutex_);
  try
  {
    const auto before = read_profiles();
    if (!contains_name(before.names, selection.profile_name))
      return ObsConfigurationFailure{ObsConfigurationFailureCode::profile_not_found,
                                     before.current_name};
    if (before.current_name == selection.profile_name)
      return ObsConfigurationSelectionResult{false, before.current_name, before.current_name};
    if (before.current_name != selection.expected_current_name)
      return ObsConfigurationFailure{ObsConfigurationFailureCode::profile_precondition_failed,
                                     before.current_name};
    if (live_output_active())
      return ObsConfigurationFailure{ObsConfigurationFailureCode::live_output_active,
                                     before.current_name};

    obs_.request("SetCurrentProfile",
                 {{"profileName", selection.profile_name}},
                 std::source_location::current(),
                 "select the profile authorized by the paired client");
    const auto after = read_profiles();
    if (after.current_name != selection.profile_name)
      return ObsConfigurationFailure{ObsConfigurationFailureCode::verification_failed,
                                     after.current_name};

    audit_.command(device.id, "set-obs-profile", selection.profile_name, "accepted", "");
    if (request_reconcile_)
      request_reconcile_();
    return ObsConfigurationSelectionResult{true, before.current_name, after.current_name};
  }
  catch (...)
  {
    return ObsConfigurationFailure{ObsConfigurationFailureCode::configuration_unavailable};
  }
}

ObsConfigurationSelectionOutcome
ObsConfigurationService::select_scene_collection(const Device& device,
                                                 const ObsSceneCollectionSelection& selection)
{
  if (const auto failure = authorize(device))
    return *failure;
  std::lock_guard operation_lock(operation_mutex_);
  try
  {
    const auto before = read_scene_collections();
    if (!contains_name(before.names, selection.scene_collection_name))
      return ObsConfigurationFailure{ObsConfigurationFailureCode::scene_collection_not_found,
                                     before.current_name};
    if (before.current_name == selection.scene_collection_name)
      return ObsConfigurationSelectionResult{false, before.current_name, before.current_name};
    if (before.current_name != selection.expected_current_name)
    {
      return ObsConfigurationFailure{
          ObsConfigurationFailureCode::scene_collection_precondition_failed, before.current_name};
    }
    if (live_output_active())
      return ObsConfigurationFailure{ObsConfigurationFailureCode::live_output_active,
                                     before.current_name};

    obs_.request("SetCurrentSceneCollection",
                 {{"sceneCollectionName", selection.scene_collection_name}},
                 std::source_location::current(),
                 "select the scene collection authorized by the paired client");
    const auto after = read_scene_collections();
    if (after.current_name != selection.scene_collection_name)
      return ObsConfigurationFailure{ObsConfigurationFailureCode::verification_failed,
                                     after.current_name};

    audit_.command(
        device.id, "set-obs-scene-collection", selection.scene_collection_name, "accepted", "");
    if (request_reconcile_)
      request_reconcile_();
    return ObsConfigurationSelectionResult{true, before.current_name, after.current_name};
  }
  catch (...)
  {
    return ObsConfigurationFailure{ObsConfigurationFailureCode::configuration_unavailable};
  }
}

} // namespace studio_remote
