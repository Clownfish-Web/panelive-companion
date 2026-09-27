#pragma once

#include <studio_remote/ipc/source_thumbnail_mapping.hpp>

#include <cstdint>
#include <span>

namespace studio_remote::obs_plugin
{
/// Called by the render-thread publisher with locally owned pixels. Shared reply fields never
/// determine the copy extent. Invalid success payloads publish render_failed without pixels.
void publish_thumbnail_response(ipc::SourceThumbnailMapping& mapping,
                                std::uint64_t sequence,
                                ipc::ThumbnailStatus status,
                                std::span<const std::uint8_t> pixels,
                                std::uint32_t width,
                                std::uint32_t height,
                                std::uint32_t stride,
                                std::uint64_t captured_ns);
} // namespace studio_remote::obs_plugin
