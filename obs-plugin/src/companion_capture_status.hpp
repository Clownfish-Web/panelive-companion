#pragma once

#include <cstdint>
#include <string>

namespace studio_remote::obs_plugin
{

enum class CompanionCaptureState
{
  idle,
  starting,
  active,
  stopping,
  faulted,
};

struct CompanionStatusSnapshot
{
  std::uint64_t generation = 0;
  std::uint32_t active_video_client_count = 0;
  CompanionCaptureState capture_state = CompanionCaptureState::idle;
  std::uint32_t requested_program_fps = 0;
  std::uint32_t requested_preview_fps = 0;
  bool companion_heartbeat_fresh = false;
  std::string status_reason = "waiting-for-client";
};

} // namespace studio_remote::obs_plugin
