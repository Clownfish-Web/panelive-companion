#include "windows_obs_quick_transition_catalog.hpp"

#include <Windows.h>
#include <cstdlib>
#include <memory>
#include <studio_remote/obs/obs_saved_quick_transition_catalog.hpp>
#include <utility>

namespace studio_remote::windows
{
json load_obs_quick_transition_catalog(const std::filesystem::path& configuration_root,
                                       const json& obs_transition_catalog)
{
  auto result = read_obs_saved_quick_transition_catalog(configuration_root, obs_transition_catalog);
  if (result.status == ObsSavedCatalogStatus::invalid_fields)
    OutputDebugStringA("OBS saved quick-transition configuration has invalid field types\n");
  return std::move(result.transitions);
}

std::filesystem::path obs_configuration_root()
{
  char* roaming = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&roaming, &length, "APPDATA") != 0 || !roaming)
    return {};
  std::unique_ptr<char, decltype(&std::free)> value(roaming, &std::free);
  return std::filesystem::path(value.get()) / "obs-studio";
}

} // namespace studio_remote::windows
