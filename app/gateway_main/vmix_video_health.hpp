#pragma once
#include <studio_remote/common/types.hpp>

namespace studio_remote
{
class StateStore;
inline constexpr std::uint64_t vmix_video_track_epoch = 1;
/// Projects native capture health into the same client geometry/epoch as delivered frames.
/// Native diagnostic snapshots retain their source geometry. Borrows state during the callback.
void publish_vmix_capture_health(StateStore& state, std::string_view track, const json& health);
} // namespace studio_remote
