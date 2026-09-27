#pragma once

#include <cstddef>
#include <string_view>

namespace studio_remote
{
inline constexpr std::size_t maximum_sdp_offer_bytes = 64 * 1024;
inline constexpr std::size_t maximum_sdp_offer_lines = 1024;
inline constexpr std::size_t maximum_sdp_line_bytes = 4096;
inline constexpr std::size_t maximum_sdp_media_sections = 16;
inline constexpr std::size_t maximum_sdp_candidate_lines = 128;
inline constexpr std::size_t maximum_sdp_ssrc_lines = 128;

// Allocation-free admission before native SDP parsing. Counts include duplicates and rejected
// sections. This bounds parser work; the native parser still owns SDP semantic validation.
bool sdp_offer_within_limits(std::string_view sdp) noexcept;
} // namespace studio_remote
