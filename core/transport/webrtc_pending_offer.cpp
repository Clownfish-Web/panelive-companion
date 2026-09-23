#include "webrtc_pending_offer.hpp"

#include "libdatachannel_peer.hpp"

#include <iostream>
#include <utility>

namespace studio_remote
{
WebRtcHub::PendingOffer::PendingOffer(WebRtcHub& hub, std::shared_ptr<Peer> peer)
    : hub_(hub), peer_(std::move(peer))
{
  std::lock_guard lock(hub_.mutex_);
  if (hub_.stopping_)
    peer_->cancel_offer(MediaOfferFailure::hub_stopped);
  else if (hub_.pending_offers_.size() >= 64)
    peer_->cancel_offer(MediaOfferFailure::capacity_exceeded);
  else
  {
    hub_.pending_offers_.insert(peer_);
    registered_ = true;
  }
}

WebRtcHub::PendingOffer::~PendingOffer()
{
  if (!published_)
  {
    const auto failures = peer_->retire();
    if (failures != 0)
      std::cerr << "WebRTC pending offer cleanup failed for " << failures << " resources\n";
  }
  if (registered_)
  {
    std::lock_guard lock(hub_.mutex_);
    hub_.pending_offers_.erase(peer_);
    hub_.pending_offers_finished_.notify_all();
  }
}

std::optional<MediaOfferFailure> WebRtcHub::PendingOffer::failure() const
{
  return peer_->pending_failure();
}

std::optional<MediaOfferFailure> WebRtcHub::PendingOffer::publish()
{
  std::shared_ptr<Peer> replaced;
  {
    std::lock_guard lock(hub_.mutex_);
    if (const auto reason = failure())
      return reason;
    const auto found = hub_.peers_.find(peer_->device_id);
    if (found != hub_.peers_.end())
      replaced = std::move(found->second);
    hub_.peers_[peer_->device_id] = peer_;
    ++hub_.video_demand_epoch_;
    published_ = true;
  }
  if (replaced)
    hub_.retire_peer(replaced, "peerReplaced");
  return std::nullopt;
}
} // namespace studio_remote
