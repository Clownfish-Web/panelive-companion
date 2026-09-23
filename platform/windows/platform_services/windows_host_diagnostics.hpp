#pragma once

#include <studio_remote/common/types.hpp>

namespace studio_remote::windows
{

// Returns one bounded scalar snapshot of the Companion process, host memory/power state, and active
// network interfaces. It performs no network I/O and exposes no addresses or interface secrets.
json windows_host_diagnostics();

} // namespace studio_remote::windows
