#pragma once

#include "libdatachannel_transport.hpp"

namespace studio_remote
{
// The request thread owns construction and unpublished cleanup. Removal/reset/stop can only
// cancel that construction; publishing transfers retirement authority to the hub's peer map.
class WebRtcHub::PendingOffer
{
public:
  PendingOffer(WebRtcHub& hub, std::shared_ptr<Peer> peer);
  ~PendingOffer();
  PendingOffer(const PendingOffer&) = delete;
  PendingOffer& operator=(const PendingOffer&) = delete;

  std::optional<MediaOfferFailure> failure() const;
  std::optional<MediaOfferFailure> publish();

private:
  bool has_device_capacity_locked() const;
  WebRtcHub& hub_;
  std::shared_ptr<Peer> peer_;
  bool registered_ = false;
  bool published_ = false;
};
} // namespace studio_remote
