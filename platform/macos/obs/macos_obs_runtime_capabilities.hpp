#pragma once

#include "../ipc/macos_ipc_names.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace studio_remote::macos
{
/// Reads the helper's authoritative pause capability. Missing, stale, or incompatible data means
/// unavailable. Each observation reopens the name so producer unlink/restart cannot pin stale IPC.
class MacosObsRuntimeCapabilities final
{
public:
  using MonotonicClock = std::function<std::uint64_t()>;
  explicit MacosObsRuntimeCapabilities(std::string name = ipc_names::runtime_capabilities);
  MacosObsRuntimeCapabilities(std::string name, MonotonicClock clock);
  bool recording_pause_available() const;

private:
  const std::string name_;
  const MonotonicClock clock_;
};
} // namespace studio_remote::macos
