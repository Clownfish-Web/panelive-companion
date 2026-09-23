#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace studio_remote::ipc
{

inline constexpr wchar_t source_thumbnail_mapping_name[] =
    L"Local\\panelive-companion.source-thumbnails.v1";
inline constexpr char source_thumbnail_shared_memory_name[] =
    "/clownfishweb-panelive-source-thumbnails-v1";
inline constexpr std::uint32_t source_thumbnail_magic = 0x53525448;
inline constexpr std::uint32_t source_thumbnail_version = 1;
inline constexpr std::uint32_t maximum_thumbnail_width = 320;
inline constexpr std::uint32_t maximum_thumbnail_height = 320;
inline constexpr std::uint32_t thumbnail_bytes_per_pixel = 4;
inline constexpr std::size_t maximum_thumbnail_bytes =
    static_cast<std::size_t>(maximum_thumbnail_width) * maximum_thumbnail_height *
    thumbnail_bytes_per_pixel;

enum class ThumbnailStatus : std::uint32_t
{
  idle = 0,
  success = 1,
  source_not_found = 2,
  unsupported_source = 3,
  render_failed = 4,
  invalid_request = 5,
  source_initialization_timeout = 6,
  activation_required = 7,
  source_unavailable = 8
};

struct SourceThumbnailMapping
{
  std::uint32_t magic = source_thumbnail_magic;
  std::uint32_t version = source_thumbnail_version;
  std::uint32_t structure_size = 0;
  std::uint32_t producer_process_id = 0;

  char source_id[129]{};
  char source_name[256]{};
  std::uint32_t maximum_width = 0;
  std::uint32_t maximum_height = 0;
  alignas(8) std::atomic<std::uint64_t> request_sequence{0};

  std::uint32_t status = static_cast<std::uint32_t>(ThumbnailStatus::idle);
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  std::uint32_t data_size = 0;
  std::uint64_t captured_ns = 0;
  std::uint8_t pixels[maximum_thumbnail_bytes]{};
  alignas(8) std::atomic<std::uint64_t> response_sequence{0};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

} // namespace studio_remote::ipc
