#include "vmix_video_health.hpp"
#include "../../core/transport/libdatachannel_transport.hpp"
#include <studio_remote/state/state_store.hpp>

namespace studio_remote
{
void publish_vmix_capture_health(StateStore& state, std::string_view track, const json& health)
{
  // Native adapters report source geometry (SRT) or delivered geometry (NDI), without the
  // delivery epoch. Compare the same client-facing fields as the frame callback; otherwise
  // ordinary health updates invalidate unrelated commands through the global state sequence.
  const auto width = health.value("deliveredWidth", health.value("width", std::uint32_t{0}));
  const auto height = health.value("deliveredHeight", health.value("height", std::uint32_t{0}));
  const auto encoded = encoded_video_dimensions(width, height);
  auto delivered_health = health;
  delivered_health["trackEpoch"] = vmix_video_track_epoch;
  delivered_health["width"] = encoded.width;
  delivered_health["height"] = encoded.height;
  state.set_video_health(track, delivered_health);
}
} // namespace studio_remote
