#pragma once

#include <optional>
#include <rtc/description.hpp>
#include <string_view>

namespace rtc
{
class PeerConnection;
}

namespace studio_remote
{
// Translate native input-validation exceptions without exposing SDP in public errors. Resource
// and internal failures still propagate; callers retain pending-peer cleanup ownership.
// Parsing rejects embedded NUL and non-decimal/out-of-range candidate ports before native use.
std::optional<rtc::Description> parse_webrtc_remote_offer(std::string_view sdp);
// Ignore candidates that cannot resolve numerically on this host; never start DNS/mDNS lookups.
bool apply_webrtc_remote_offer(rtc::PeerConnection& connection, rtc::Description offer);
} // namespace studio_remote
