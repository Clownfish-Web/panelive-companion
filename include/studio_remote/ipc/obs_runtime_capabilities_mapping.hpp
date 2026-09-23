#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

namespace studio_remote::ipc
{

inline constexpr wchar_t obs_runtime_capabilities_mapping_name[] =
    L"Local\\panelive-companion.runtime-capabilities.v1";
inline constexpr char obs_runtime_capabilities_shared_memory_name[] =
    "/clownfishweb-panelive-obs-runtime-capabilities-v1";
inline constexpr std::uint32_t obs_runtime_capabilities_magic = 0x4F425243U;
inline constexpr std::uint32_t obs_runtime_capabilities_version = 1;
inline constexpr auto obs_runtime_capabilities_timeout = std::chrono::seconds(2);

struct ObsRuntimeCapabilitiesMapping
{
  std::uint32_t magic = obs_runtime_capabilities_magic;
  std::uint32_t version = obs_runtime_capabilities_version;
  std::uint32_t structure_size = sizeof(ObsRuntimeCapabilitiesMapping);
  std::uint32_t producer_process_id = 0;
  std::atomic<std::uint64_t> generation{0};
  std::atomic<std::uint32_t> recording_pause_available{0};
  std::atomic<std::uint64_t> producer_heartbeat_ns{0};
};

struct ObsRuntimeCapabilitiesSnapshot
{
  bool recording_pause_available = false;
  std::uint64_t generation = 0;
};

inline bool compatible_obs_runtime_capabilities(const ObsRuntimeCapabilitiesMapping& mapping)
{
  return mapping.magic == obs_runtime_capabilities_magic &&
         mapping.version == obs_runtime_capabilities_version &&
         mapping.structure_size == sizeof(ObsRuntimeCapabilitiesMapping);
}

inline std::optional<ObsRuntimeCapabilitiesSnapshot>
read_obs_runtime_capabilities(const ObsRuntimeCapabilitiesMapping& mapping,
                              std::uint64_t current_time_ns)
{
  if (!compatible_obs_runtime_capabilities(mapping))
    return std::nullopt;

  for (int attempt = 0; attempt < 3; ++attempt)
  {
    const auto first_generation = mapping.generation.load(std::memory_order_acquire);
    if ((first_generation & 1U) != 0U)
      continue;
    const auto recording_pause_available =
        mapping.recording_pause_available.load(std::memory_order_relaxed) != 0;
    const auto heartbeat = mapping.producer_heartbeat_ns.load(std::memory_order_relaxed);
    const auto second_generation = mapping.generation.load(std::memory_order_acquire);
    if (first_generation != second_generation || (second_generation & 1U) != 0U)
      continue;

    const auto timeout_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(obs_runtime_capabilities_timeout)
            .count());
    if (heartbeat == 0 || current_time_ns < heartbeat || current_time_ns - heartbeat > timeout_ns)
      return std::nullopt;
    return ObsRuntimeCapabilitiesSnapshot{recording_pause_available, second_generation};
  }
  return std::nullopt;
}

} // namespace studio_remote::ipc
