#pragma once

#include <studio_remote/media/audio_monitor.hpp>

#include <atomic>
#include <cstdint>
#include <optional>

namespace studio_remote
{

struct AudioLevelSample
{
  std::uint64_t sequence = 0;
  std::uint64_t source_sample_index = 0;
  std::uint32_t topology_generation = 0;
  double rms_dbfs = -120.0;
  double peak_dbfs = -120.0;
};

// Publishes a consistent scalar snapshot of the PCM block that enters Opus. No audio payload is
// retained, and readers never block the audio producer.
class AudioLevelTelemetry
{
public:
  void observe(const AudioMonitorFrame& frame) noexcept;
  std::optional<AudioLevelSample> latest() const noexcept;

private:
  std::atomic<std::uint64_t> publication_{0};
  std::atomic<std::uint64_t> sequence_{0};
  std::atomic<std::uint64_t> source_sample_index_{0};
  std::atomic<std::uint32_t> topology_generation_{0};
  std::atomic<double> rms_dbfs_{-120.0};
  std::atomic<double> peak_dbfs_{-120.0};
};

} // namespace studio_remote
