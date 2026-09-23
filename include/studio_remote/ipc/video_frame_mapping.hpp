#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace studio_remote::ipc
{

inline constexpr wchar_t video_frame_mapping_name[] =
    L"Local\\panelive-companion.frames.v1";
inline constexpr char video_frame_shared_memory_name[] =
    "/panelive-companion-frames-v1";

inline constexpr std::uint32_t video_frame_magic = 0x5352464D;
inline constexpr std::uint32_t video_frame_mapping_version = 1;
inline constexpr std::uint32_t maximum_frame_width = 960;
inline constexpr std::uint32_t maximum_frame_height = 540;
inline constexpr std::uint32_t bytes_per_pixel = 4;
inline constexpr std::size_t maximum_frame_bytes =
    static_cast<std::size_t>(maximum_frame_width) * maximum_frame_height * bytes_per_pixel;

// The low bit selects the published slot. The remaining bits identify the capture generation.
// This preserves the frame-v1 layout while allowing consumers to fence frames across an OBS
// scene-collection reload.
inline constexpr std::uint32_t video_published_slot_index(std::uint32_t value) noexcept
{
  return value & 1U;
}

inline constexpr std::uint32_t video_capture_generation(std::uint32_t value) noexcept
{
  return value >> 1U;
}

inline constexpr std::uint32_t next_video_capture_generation(std::uint32_t value) noexcept
{
  return ((video_capture_generation(value) + 1U) & 0x7FFFFFFFU) << 1U;
}

struct VideoFrameSlot
{
  alignas(8) std::atomic<std::uint64_t> sequence{0};
  std::uint64_t captured_ns = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::uint32_t data_size = 0;
  std::uint8_t pixels[maximum_frame_bytes]{};
};

struct VideoTrackMapping
{
  alignas(4) std::atomic<std::uint32_t> published_slot{0};
  alignas(8) std::atomic<std::uint64_t> published_frames{0};
  VideoFrameSlot slots[2];
};

struct VideoFrameMapping
{
  std::uint32_t magic = video_frame_magic;
  std::uint32_t version = video_frame_mapping_version;
  std::uint32_t structure_size = 0;
  std::uint32_t producer_process_id = 0;
  alignas(8) std::atomic<std::uint64_t> producer_heartbeat_ns{0};
  VideoTrackMapping program;
  VideoTrackMapping preview;
};

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

} // namespace studio_remote::ipc
