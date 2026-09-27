#include "windows_obs_runtime_capabilities.hpp"

#include <chrono>

namespace studio_remote
{
namespace
{
std::uint64_t monotonic_time_ns()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}
} // namespace

WindowsObsRuntimeCapabilities::~WindowsObsRuntimeCapabilities()
{
  std::lock_guard lock(mutex_);
  close();
}

bool WindowsObsRuntimeCapabilities::recording_pause_available() const
{
  std::lock_guard lock(mutex_);
  if (!mapping_ && !open())
    return false;
  const auto snapshot = ipc::read_obs_runtime_capabilities(*mapping_, monotonic_time_ns());
  return snapshot && snapshot->recording_pause_available;
}

bool WindowsObsRuntimeCapabilities::open() const
{
  mapping_handle_ = windows::open_owner_restricted_mapping(
      FILE_MAP_READ, ipc::obs_runtime_capabilities_mapping_name);
  if (!mapping_handle_)
    return false;
  mapping_ = static_cast<const ipc::ObsRuntimeCapabilitiesMapping*>(MapViewOfFile(
      mapping_handle_.get(), FILE_MAP_READ, 0, 0, sizeof(ipc::ObsRuntimeCapabilitiesMapping)));
  if (!mapping_)
  {
    mapping_handle_.reset();
    return false;
  }
  return true;
}

void WindowsObsRuntimeCapabilities::close() const
{
  if (mapping_ && !UnmapViewOfFile(mapping_))
    OutputDebugStringW(L"Could not unmap OBS runtime capabilities\n");
  mapping_ = nullptr;
  mapping_handle_.reset();
}

} // namespace studio_remote
