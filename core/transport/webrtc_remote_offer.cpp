#include "webrtc_remote_offer.hpp"

#include <charconv>
#include <cstdint>
#include <rtc/peerconnection.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace studio_remote
{
namespace
{
// Candidate fields use whitespace extraction in the native parser. Validate the complete port
// token before it can reach getaddrinfo as an OS service name or a partially interpreted number.
bool valid_candidate_port(std::string_view candidate)
{
  constexpr std::string_view whitespace = " \t\r\v\f";
  std::string_view field;
  for (unsigned index = 0; index < 6; ++index)
  {
    const auto start = candidate.find_first_not_of(whitespace);
    if (start == std::string_view::npos)
      return false;
    candidate.remove_prefix(start);
    const auto end = candidate.find_first_of(whitespace);
    field = candidate.substr(0, end);
    candidate.remove_prefix(field.size());
  }
  std::uint16_t port = 0;
  const auto result = std::from_chars(field.data(), field.data() + field.size(), port);
  return result.ec == std::errc{} && result.ptr == field.data() + field.size() && port != 0;
}

bool valid_candidate_ports(std::string_view sdp)
{
  constexpr std::string_view prefix = "a=candidate:";
  while (!sdp.empty())
  {
    const auto newline = sdp.find('\n');
    const auto line = sdp.substr(0, newline);
    if (line.starts_with(prefix) && !valid_candidate_port(line.substr(prefix.size())))
      return false;
    if (newline == std::string_view::npos)
      break;
    sdp.remove_prefix(newline + 1);
  }
  return true;
}
} // namespace

std::optional<rtc::Description> parse_webrtc_remote_offer(std::string_view sdp)
{
  // Native ICE libraries consume C strings. Embedded NUL must never change their interpretation
  // of an otherwise length-delimited public request.
  if (sdp.find('\0') != std::string_view::npos || !valid_candidate_ports(sdp))
    return std::nullopt;
  try
  {
    return rtc::Description(std::string(sdp), "offer");
  }
  catch (const std::invalid_argument&)
  {
    return std::nullopt;
  }
}

bool apply_webrtc_remote_offer(rtc::PeerConnection& connection, rtc::Description offer)
{
  try
  {
    // Current native clients supply numeric host candidates. Unsupported address forms must not
    // reach libdatachannel's detached hostname resolver. Simple resolution never performs DNS;
    // keep resolved values so a later native check cannot fall back to hostname lookup.
    for (auto& candidate : offer.extractCandidates())
      if (candidate.resolve(rtc::Candidate::ResolveMode::Simple))
        offer.addCandidate(std::move(candidate));
    connection.setRemoteDescription(std::move(offer));
    return true;
  }
  catch (const std::invalid_argument&)
  {
    return false;
  }
}
} // namespace studio_remote
