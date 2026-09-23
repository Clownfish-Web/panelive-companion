#pragma once

#include <filesystem>
#include <studio_remote/obs/obs_quick_transition_catalog.hpp>

namespace studio_remote::windows
{
json load_obs_quick_transition_catalog(const std::filesystem::path& obs_configuration_root,
                                       const json& obs_transition_catalog);
std::filesystem::path obs_configuration_root();
} // namespace studio_remote::windows
