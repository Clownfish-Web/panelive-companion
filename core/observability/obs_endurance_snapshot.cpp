#include <studio_remote/observability/obs_endurance_snapshot.hpp>

#include <stdexcept>
#include <utility>

namespace studio_remote
{
namespace
{
json device_snapshot(const std::vector<Device>& devices)
{
  json result = json::array();
  for (const auto& device : devices)
    result.push_back({{"id", device.id},
                      {"name", device.name},
                      {"role", role_name(device.role)},
                      {"productionProvider", device.production_provider},
                      {"lastSeenUnixMs", device.last_seen_unix_ms}});
  return result;
}

json monitor_snapshot(const AudioMonitorStatus& monitor)
{
  return {{"supported", monitor.supported},
          {"configured", monitor.configured},
          {"available", monitor.available},
          {"reason", monitor.reason},
          {"sampleRate", monitor.sample_rate},
          {"channels", monitor.channels},
          {"ageMs", monitor.age_ms},
          {"producerDroppedFrames", monitor.dropped_frames},
          {"ipcSkippedFrames", monitor.ipc_skipped_frames},
          {"timestampDiscontinuities", monitor.timestamp_discontinuities},
          {"ingressOverflowDrops", monitor.ingress_overflow_drops},
          {"oversizedCallbackDrops", monitor.oversized_callback_drops},
          {"mixerDeadlineMisses", monitor.mixer_deadline_misses},
          {"timelineGaps", monitor.timeline_gaps},
          {"timelineOverlaps", monitor.timeline_overlaps},
          {"ipcSampleIndexGaps", monitor.ipc_sample_index_gaps},
          {"latestPeak", monitor.latest_peak},
          {"latestRms", monitor.latest_rms},
          {"latestDc", monitor.latest_dc}};
}

json collect_snapshot(const ObsEnduranceSnapshotSources& sources)
{
  const auto monitor = sources.audio_monitor();
  auto devices = device_snapshot(sources.devices());
  json result;
  result["host"] = sources.host();
  result["state"] = sources.state();
  const auto connected = sources.obs_connected();
  const auto fresh = sources.state_fresh();
  auto error = sources.runtime_error();
  if (error.empty())
    error = sources.obs_error();
  result["obs"] = {
      {"connected", connected}, {"stateFresh", fresh}, {"lastError", std::move(error)}};
  result["capture"] = sources.capture();
  result["audioMonitorIpc"] = monitor_snapshot(monitor);
  result["audioMixer"] = sources.audio_mixer();
  result["thumbnails"] = sources.thumbnails();
  result["sceneItemProperties"] = sources.scene_item_properties();
  result["criticalLiveControl"] = sources.critical_live_control();
  result["webrtc"] = sources.webrtc();
  result["networkServer"] = sources.network_server();
  result["devices"] = std::move(devices);
  return result;
}
} // namespace

EnduranceSnapshotSource::Provider
make_obs_endurance_snapshot_provider(ObsEnduranceSnapshotSources sources)
{
  if (!sources.audio_monitor || !sources.devices || !sources.host || !sources.state ||
      !sources.obs_connected || !sources.state_fresh || !sources.runtime_error ||
      !sources.obs_error || !sources.capture || !sources.audio_mixer || !sources.thumbnails ||
      !sources.scene_item_properties || !sources.critical_live_control || !sources.webrtc ||
      !sources.network_server)
    throw std::invalid_argument("Endurance snapshot observation callbacks are required");
  return [sources = std::move(sources)] { return collect_snapshot(sources); };
}
} // namespace studio_remote
