#pragma once

#include <filesystem>
#include <studio_remote/common/types.hpp>

namespace studio_remote
{
enum class ObsSavedCatalogStatus
{
  available,
  unavailable,
  invalid_fields
};

struct ObsSavedQuickTransitionCatalog
{
  ObsSavedCatalogStatus status = ObsSavedCatalogStatus::unavailable;
  json transitions = json::array();
};

/// Shared platform file adapter. Reads at most 1 MiB of global.ini and 16 MiB of the selected
/// scene collection, validates its filename, and maps buttons using the shared Windows logic.
/// Missing, unreadable or malformed optional files produce an unavailable empty catalog.
/// Empty or relative roots are unavailable; never fall back to the process working directory.
/// Invalid mapped fields are reported separately for fixed-text logging at the native boundary.
ObsSavedQuickTransitionCatalog
read_obs_saved_quick_transition_catalog(const std::filesystem::path& configuration_root,
                                        const json& obs_transition_catalog);
} // namespace studio_remote
