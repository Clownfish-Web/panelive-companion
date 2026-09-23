#include "performance_sampler.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace studio_remote
{
namespace
{

std::optional<double> optional_number(const json& value, std::string_view key)
{
  const auto field = value.find(std::string(key));
  if (field == value.end())
    return std::nullopt;
  if (!field->is_number())
    throw std::runtime_error("Performance metric is not numeric: " + std::string(key));
  const auto result = field->get<double>();
  if (!std::isfinite(result) || result < 0.0)
    throw std::runtime_error("Performance metric is outside its valid range: " + std::string(key));
  return result;
}

std::optional<std::uint64_t> optional_count(const json& value, std::string_view key)
{
  const auto number = optional_number(value, key);
  if (!number)
    return std::nullopt;
  if (std::floor(*number) != *number ||
      *number >= static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
    throw std::runtime_error("Performance counter is outside its valid range: " + std::string(key));
  return static_cast<std::uint64_t>(*number);
}

} // namespace

PerformanceSampler::PerformanceSampler(IObsAdapter& obs, StateStore& state)
    : obs_(obs), state_(state)
{
}

void PerformanceSampler::sample()
{
  state_.set_performance(parse(obs_.request_current(
      "GetPerformance", {}, std::source_location::current(), "sample production performance")));
}

ProductionPerformanceSample PerformanceSampler::parse(const json& value)
{
  if (!value.is_object())
    throw std::runtime_error("Production performance response must be an object");

  ProductionPerformanceSample result;
  result.cpu_usage_percent = optional_number(value, "cpuUsagePercent");
  result.memory_usage_mib = optional_number(value, "memoryUsageMiB");
  result.available_disk_space_mib = optional_number(value, "availableDiskSpaceMiB");
  result.frames_per_second = optional_number(value, "framesPerSecond");
  result.average_frame_time_ms = optional_number(value, "averageFrameTimeMs");
  result.render_skipped_frames = optional_count(value, "renderSkippedFrames");
  result.render_total_frames = optional_count(value, "renderTotalFrames");
  result.encode_skipped_frames = optional_count(value, "encodeSkippedFrames");
  result.encode_total_frames = optional_count(value, "encodeTotalFrames");
  result.stream_bitrate_kbps = optional_number(value, "streamBitrateKbps");
  result.stream_congestion_ratio = optional_number(value, "streamCongestionRatio");
  result.stream_dropped_frames = optional_count(value, "streamDroppedFrames");
  result.stream_dropped_frames_percent = optional_number(value, "streamDroppedFramesPercent");
  result.stream_bytes = optional_count(value, "streamBytes");
  result.stream_duration_ms = optional_count(value, "streamDurationMs");

  if (result.cpu_usage_percent && *result.cpu_usage_percent > 100.0)
    throw std::runtime_error("CPU usage percentage exceeds 100");
  if (result.stream_congestion_ratio && *result.stream_congestion_ratio > 1.0)
    throw std::runtime_error("Stream congestion ratio exceeds 1");
  if (result.stream_dropped_frames_percent && *result.stream_dropped_frames_percent > 100.0)
    throw std::runtime_error("Dropped-frame percentage exceeds 100");
  return result;
}

} // namespace studio_remote
