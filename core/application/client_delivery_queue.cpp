#include <studio_remote/common/client_request_lifetime.hpp>
#include <studio_remote/events/client_delivery_queue.hpp>

namespace studio_remote
{
bool ClientDeliveryQueue::push(std::shared_ptr<const std::string> message,
                               ClientDeliveryPriority priority,
                               Clock::time_point now)
{
  std::lock_guard lock(mutex_);
  if (failed_)
    return false;
  if (!message || messages_ == maximum_messages || message->size() > maximum_bytes - bytes_)
  {
    fail_locked();
    return false;
  }
  const auto bytes = message->size();
  lanes_[static_cast<std::size_t>(priority)].push_back({std::move(message), now});
  bytes_ += bytes;
  ++messages_;
  return true;
}

std::shared_ptr<const std::string> ClientDeliveryQueue::pop(Clock::time_point now)
{
  std::lock_guard lock(mutex_);
  for (const auto& lane : lanes_)
    if (!lane.empty() && now - lane.front().queued_at >= maximum_client_request_age)
    {
      fail_locked();
      return {};
    }
  for (auto& lane : lanes_)
  {
    if (lane.empty())
      continue;
    auto message = std::move(lane.front().message);
    lane.pop_front();
    --messages_;
    bytes_ -= message->size();
    return message;
  }
  return {};
}

void ClientDeliveryQueue::fail_locked()
{
  failed_ = true;
  for (auto& lane : lanes_)
    lane.clear();
  messages_ = 0;
  bytes_ = 0;
}

bool ClientDeliveryQueue::failed() const
{
  std::lock_guard lock(mutex_);
  return failed_;
}
} // namespace studio_remote
