#include <studio_remote/transport/webrtc_offer.hpp>

#include <stdexcept>

namespace studio_remote
{

bool audio_track_requested(const json& offer, std::string_view name)
{
  const auto found = offer.find(std::string(name));
  if (found == offer.end())
    return false;
  if (!found->is_object())
    throw std::invalid_argument(std::string(name) + " must be an object");
  const auto enabled = found->find("enabled");
  if (enabled == found->end())
    return false;
  if (!enabled->is_boolean())
    throw std::invalid_argument(std::string(name) + ".enabled must be a boolean");
  return enabled->get<bool>();
}

bool audio_monitor_requested(const json& offer)
{
  return audio_track_requested(offer, "audioMonitor");
}

bool audio_master_requested(const json& offer)
{
  return audio_track_requested(offer, "audioMaster");
}

} // namespace studio_remote
