#pragma once

#include <Windows.h>
#include <mutex>
#include <studio_remote/ipc/obs_runtime_capabilities_mapping.hpp>

namespace studio_remote
{

class WindowsObsRuntimeCapabilities
{
public:
  WindowsObsRuntimeCapabilities() = default;
  ~WindowsObsRuntimeCapabilities();

  WindowsObsRuntimeCapabilities(const WindowsObsRuntimeCapabilities&) = delete;
  WindowsObsRuntimeCapabilities& operator=(const WindowsObsRuntimeCapabilities&) = delete;

  bool recording_pause_available() const;

private:
  bool open() const;
  void close() const;

  mutable std::mutex mutex_;
  mutable HANDLE mapping_handle_ = nullptr;
  mutable const ipc::ObsRuntimeCapabilitiesMapping* mapping_ = nullptr;
};

} // namespace studio_remote
