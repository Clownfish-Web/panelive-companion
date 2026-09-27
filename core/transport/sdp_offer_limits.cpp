#include <studio_remote/transport/sdp_offer_limits.hpp>

namespace studio_remote
{
bool sdp_offer_within_limits(std::string_view sdp) noexcept
{
  if (sdp.size() > maximum_sdp_offer_bytes)
    return false;
  std::size_t lines = 0;
  std::size_t media_sections = 0;
  std::size_t candidates = 0;
  std::size_t ssrc_lines = 0;
  while (!sdp.empty())
  {
    const auto newline = sdp.find('\n');
    auto line = sdp.substr(0, newline);
    if (line.ends_with('\r'))
      line.remove_suffix(1);
    if (++lines > maximum_sdp_offer_lines || line.size() > maximum_sdp_line_bytes)
      return false;
    // The native parser recognizes these prefixes only at the start of a line.
    if (line.starts_with("m=") && ++media_sections > maximum_sdp_media_sections)
      return false;
    if (line.starts_with("a=candidate:") && ++candidates > maximum_sdp_candidate_lines)
      return false;
    if (line.starts_with("a=ssrc:") && ++ssrc_lines > maximum_sdp_ssrc_lines)
      return false;
    if (newline == std::string_view::npos)
      break;
    sdp.remove_prefix(newline + 1);
  }
  return true;
}
} // namespace studio_remote
