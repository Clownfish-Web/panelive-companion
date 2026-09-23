#pragma once

#include <studio_remote/ipc/obs_runtime_capabilities_mapping.hpp>

#include <mutex>
#include <string>

namespace studio_remote::linux
{

class LinuxObsRuntimeCapabilities
{
public:
  explicit LinuxObsRuntimeCapabilities(
      std::string mapping_name = ipc::obs_runtime_capabilities_shared_memory_name);
  ~LinuxObsRuntimeCapabilities();

  LinuxObsRuntimeCapabilities(const LinuxObsRuntimeCapabilities&) = delete;
  LinuxObsRuntimeCapabilities& operator=(const LinuxObsRuntimeCapabilities&) = delete;

  bool recording_pause_available() const;

private:
  bool open() const;
  void close() const;

  std::string mapping_name_;
  mutable std::mutex mutex_;
  mutable int mapping_descriptor_ = -1;
  mutable const ipc::ObsRuntimeCapabilitiesMapping* mapping_ = nullptr;
};

} // namespace studio_remote::linux
