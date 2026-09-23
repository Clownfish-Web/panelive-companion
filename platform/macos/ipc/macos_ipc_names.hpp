#pragma once

namespace studio_remote::macos::ipc_names
{
// Darwin POSIX shared-memory names have a smaller limit than Linux. Producer and consumer must
// use these native names with the unchanged versioned IPC structures under studio_remote/ipc.
inline constexpr char runtime_capabilities[] = "/panelive-companion.rc.v1";
inline constexpr char video_frames[] = "/panelive-companion.vf.v1";
inline constexpr char video_capture_control[] = "/panelive-companion.vc.v2";
inline constexpr char audio_mixer_roster[] = "/panelive-companion.ar.v1";
inline constexpr char scene_item_properties[] = "/panelive-companion.sp.v1";
inline constexpr char quick_transition_control[] = "/panelive-companion.qt.v4";
inline constexpr char source_thumbnails[] = "/panelive-companion.th.v1";
inline constexpr char audio_monitor[] = "/panelive-companion.am.v3";
inline constexpr char audio_monitor_control[] = "/panelive-companion.ac.v2";
static_assert(sizeof(runtime_capabilities) <= 31);
static_assert(sizeof(video_frames) <= 31);
static_assert(sizeof(video_capture_control) <= 31);
static_assert(sizeof(audio_mixer_roster) <= 31);
static_assert(sizeof(scene_item_properties) <= 31);
static_assert(sizeof(quick_transition_control) <= 31);
static_assert(sizeof(source_thumbnails) <= 31);
static_assert(sizeof(audio_monitor) <= 31);
static_assert(sizeof(audio_monitor_control) <= 31);
} // namespace studio_remote::macos::ipc_names
