#include "macos_obs_runtime_capabilities.hpp"

#include "../ipc/read_only_shared_memory.hpp"

#include <stdexcept>
#include <studio_remote/ipc/obs_runtime_capabilities_mapping.hpp>
#include <time.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
std::uint64_t obs_uptime_ns()
{
  // Match libobs os_gettime_ns. libc++ steady_clock includes sleep, while OBS uptime does not.
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}
} // namespace
MacosObsRuntimeCapabilities::MacosObsRuntimeCapabilities(std::string name)
    : MacosObsRuntimeCapabilities(std::move(name), obs_uptime_ns)
{
}
MacosObsRuntimeCapabilities::MacosObsRuntimeCapabilities(std::string name, MonotonicClock clock)
    : name_(std::move(name)), clock_(std::move(clock))
{
  if (!clock_)
    throw std::invalid_argument("OBS runtime capabilities require a monotonic clock");
}
bool MacosObsRuntimeCapabilities::recording_pause_available() const
{
  const auto region = ReadOnlySharedMemory::open(name_, sizeof(ipc::ObsRuntimeCapabilitiesMapping));
  if (!region)
    return false;
  const auto* mapping =
      reinterpret_cast<const ipc::ObsRuntimeCapabilitiesMapping*>(region->bytes().data());
  const auto snapshot = ipc::read_obs_runtime_capabilities(*mapping, clock_());
  return snapshot && snapshot->recording_pause_available;
}
} // namespace studio_remote::macos
