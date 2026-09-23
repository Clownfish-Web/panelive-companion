#pragma once

#include <cstdint>

namespace studio_remote::obs_plugin
{

enum class QuickTransitionDurationUpdateResult
{
  updated,
  control_not_found,
  invocation_failed,
};

QuickTransitionDurationUpdateResult update_quick_transition_duration(std::int32_t transition_id,
                                                                     std::uint32_t duration_ms);

} // namespace studio_remote::obs_plugin
