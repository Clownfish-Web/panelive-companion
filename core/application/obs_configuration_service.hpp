#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/obs/obs_adapter.hpp>
#include <utility>
#include <variant>
#include <vector>

namespace studio_remote
{

struct ObsProfileCatalog
{
  std::string current_name;
  std::vector<std::string> names;
};

struct ObsSceneCollectionCatalog
{
  std::string current_name;
  std::vector<std::string> names;
};

struct ObsProfileSelection
{
  std::string profile_name;
  std::string expected_current_name;
};

struct ObsSceneCollectionSelection
{
  std::string scene_collection_name;
  std::string expected_current_name;
};

struct ObsConfigurationSelectionResult
{
  bool changed = false;
  std::string previous_name;
  std::string current_name;
};

enum class ObsConfigurationFailureCode
{
  operator_role_required,
  unsupported,
  backend_unavailable,
  profile_precondition_failed,
  scene_collection_precondition_failed,
  profile_not_found,
  scene_collection_not_found,
  live_output_active,
  verification_failed,
  configuration_unavailable
};

struct ObsConfigurationFailure
{
  explicit ObsConfigurationFailure(ObsConfigurationFailureCode failure_code,
                                   std::string active_name = {})
      : code(failure_code), current_name(std::move(active_name))
  {
  }

  ObsConfigurationFailureCode code = ObsConfigurationFailureCode::configuration_unavailable;
  std::string current_name;
};

using ObsProfileCatalogResult = std::variant<ObsProfileCatalog, ObsConfigurationFailure>;
using ObsSceneCollectionCatalogResult =
    std::variant<ObsSceneCollectionCatalog, ObsConfigurationFailure>;
using ObsConfigurationSelectionOutcome =
    std::variant<ObsConfigurationSelectionResult, ObsConfigurationFailure>;

class ObsConfigurationService
{
public:
  using ReconcileRequest = std::function<void()>;

  ObsConfigurationService(IObsAdapter& obs, AuditStore& audit, ReconcileRequest request_reconcile);

  ObsProfileCatalogResult profiles(const Device& device);
  ObsSceneCollectionCatalogResult scene_collections(const Device& device);
  ObsConfigurationSelectionOutcome select_profile(const Device& device,
                                                  const ObsProfileSelection& selection);
  ObsConfigurationSelectionOutcome
  select_scene_collection(const Device& device, const ObsSceneCollectionSelection& selection);

private:
  std::optional<ObsConfigurationFailure> authorize(const Device& device) const;
  ObsProfileCatalog read_profiles();
  ObsSceneCollectionCatalog read_scene_collections();
  bool live_output_active();

  IObsAdapter& obs_;
  AuditStore& audit_;
  ReconcileRequest request_reconcile_;
  std::mutex operation_mutex_;
};

} // namespace studio_remote
