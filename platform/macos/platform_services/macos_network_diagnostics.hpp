#pragma once

#include <studio_remote/common/types.hpp>

namespace studio_remote::macos
{
/// Local route-table counters with private identifiers omitted. Fixed observation failures are
/// appended to errors so other host/process observations remain available.
json macos_network_diagnostics(json& errors);
} // namespace studio_remote::macos
