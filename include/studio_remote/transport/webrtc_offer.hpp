#pragma once

#include <studio_remote/common/types.hpp>

namespace studio_remote
{

bool audio_monitor_requested(const json& offer);
bool audio_master_requested(const json& offer);

} // namespace studio_remote
