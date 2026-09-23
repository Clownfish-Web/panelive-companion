#pragma once

#include <studio_remote/obs/obs_adapter.hpp>

#include <optional>
#include <string>
#include <vector>

namespace studio_remote
{

std::optional<std::vector<std::string>>
read_audio_meter_input_names(const NormalizedObsEvent& event);

} // namespace studio_remote
