#pragma once

#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/media/audio_monitor.hpp>
#include <studio_remote/observability/endurance_snapshot_source.hpp>

#include <functional>
#include <string>
#include <vector>

namespace studio_remote
{
/// Borrowed synchronous observation views, supplied by the composition. Every callback reads
/// local thread-safe state; none may issue OBS/network requests, activate media or access UI or
/// credentials. The resulting snapshot is a diagnostic serialization boundary, not a mobile DTO.
struct ObsEnduranceSnapshotSources
{
  std::function<AudioMonitorStatus()> audio_monitor;
  std::function<std::vector<Device>()> devices;
  std::function<json()> host;
  std::function<json()> state;
  std::function<bool()> obs_connected;
  std::function<bool()> state_fresh;
  std::function<std::string()> runtime_error;
  std::function<std::string()> obs_error;
  std::function<json()> capture;
  std::function<json()> audio_mixer;
  std::function<json()> thumbnails;
  std::function<json()> scene_item_properties;
  std::function<json()> critical_live_control;
  std::function<json()> webrtc;
  std::function<json()> network_server;
};

/// Retains the callbacks, not their borrowed dependencies. The composition registers this through
/// EnduranceSnapshotRegistration and clears/joins it before destroying those dependencies.
EnduranceSnapshotSource::Provider
make_obs_endurance_snapshot_provider(ObsEnduranceSnapshotSources sources);
} // namespace studio_remote
