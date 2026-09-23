#pragma once

#include <cstdint>

namespace studio_remote
{

struct VideoDemand
{
  std::uint32_t active_video_client_count = 0;
  std::uint32_t requested_program_fps = 0;
  std::uint32_t requested_preview_fps = 0;
  std::uint64_t epoch = 0;

  bool operator==(const VideoDemand&) const = default;
};

} // namespace studio_remote
