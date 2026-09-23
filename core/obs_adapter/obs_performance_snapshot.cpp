#include <studio_remote/obs/obs_performance_snapshot.hpp>

#include <studio_remote/obs/obs_adapter.hpp>

namespace studio_remote
{
json obs_performance_snapshot(IObsAdapter& adapter, std::source_location caller)
{
  const auto stats = adapter.request("GetStats", {}, caller, "sample OBS performance statistics");
  const auto stream =
      adapter.request("GetStreamStatus", {}, caller, "sample OBS stream statistics");
  json result = json::object();
  const auto copy = [](json& target,
                       std::string_view target_name,
                       const json& source,
                       std::string_view source_name)
  {
    const auto value = source.find(std::string(source_name));
    if (value != source.end() && value->is_number())
      target[std::string(target_name)] = *value;
  };
  copy(result, "cpuUsagePercent", stats, "cpuUsage");
  copy(result, "memoryUsageMiB", stats, "memoryUsage");
  copy(result, "availableDiskSpaceMiB", stats, "availableDiskSpace");
  copy(result, "framesPerSecond", stats, "activeFps");
  copy(result, "averageFrameTimeMs", stats, "averageFrameRenderTime");
  copy(result, "renderSkippedFrames", stats, "renderSkippedFrames");
  copy(result, "renderTotalFrames", stats, "renderTotalFrames");
  copy(result, "encodeSkippedFrames", stats, "outputSkippedFrames");
  copy(result, "encodeTotalFrames", stats, "outputTotalFrames");
  copy(result, "streamCongestionRatio", stream, "outputCongestion");
  copy(result, "streamBytes", stream, "outputBytes");
  copy(result, "streamDurationMs", stream, "outputDuration");
  return result;
}
} // namespace studio_remote
