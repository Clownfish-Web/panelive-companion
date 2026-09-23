#include "macos_obs_quick_transition_catalog.hpp"

#include <studio_remote/obs/obs_saved_quick_transition_catalog.hpp>
#include <syslog.h>
#include <utility>

namespace studio_remote::macos
{
json load_obs_quick_transition_catalog(const std::filesystem::path& configuration_root,
                                       const json& obs_transition_catalog)
{
  auto result = read_obs_saved_quick_transition_catalog(configuration_root, obs_transition_catalog);
  if (result.status == ObsSavedCatalogStatus::invalid_fields)
    syslog(LOG_WARNING, "OBS saved quick-transition configuration has invalid field types");
  return std::move(result.transitions);
}
} // namespace studio_remote::macos
