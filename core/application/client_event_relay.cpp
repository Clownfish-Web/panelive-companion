#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <studio_remote/events/client_event_relay.hpp>
#include <utility>

namespace studio_remote
{
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

ClientEventConnection::ClientEventConnection(ClientEventOrigin origin,
                                             std::uint32_t permissions,
                                             std::shared_ptr<ClientEventAuthority> authority,
                                             std::size_t slot_index,
                                             std::atomic<std::uint32_t>& relay_wake,
                                             std::function<bool(std::string_view)> writer,
                                             std::function<void(ClientDeliveryFailure)> failure)
    : origin_(std::move(origin)), permissions_(permissions), authority_(std::move(authority)),
      slot_index_(slot_index), relay_wake_(relay_wake), writer_(std::move(writer)),
      failure_(std::move(failure)), writer_thread_([this] { write_loop(); })
{
}
bool ClientEventConnection::admit_frame(std::size_t payload_bytes, Clock::time_point now) noexcept
{
  return ready() && frame_budget_.admit(payload_bytes, now);
}

bool ClientEventConnection::submit(ClientEventNotification notification)
{
  if (!active_.load(std::memory_order_acquire) || !ready_.load(std::memory_order_acquire))
    return false;
  auto* pending = inbound_.try_reserve_write();
  if (!pending)
    return false;
  *pending = {std::move(notification), Clock::now()};
  inbound_.commit_write();
  relay_wake_.fetch_add(1, std::memory_order_release);
  relay_wake_.notify_one();
  return true;
}
void ClientEventConnection::set_permissions(std::uint32_t permissions) noexcept
{
  permissions_.store(permissions, std::memory_order_release);
}
void ClientEventConnection::revoke() noexcept
{
  active_.store(false, std::memory_order_release);
}
void ClientEventConnection::request_revocation_close() noexcept
{
  revoke();
  revocation_close_.store(true, std::memory_order_release);
  writer_wake_.fetch_add(1, std::memory_order_release);
  writer_wake_.notify_one();
}
void ClientEventConnection::activate() noexcept
{
  ready_.store(true, std::memory_order_release);
  writer_wake_.fetch_add(1, std::memory_order_release);
  writer_wake_.notify_one();
}
const ClientEventOrigin& ClientEventConnection::origin() const noexcept
{
  return origin_;
}
bool ClientEventConnection::ready() const noexcept
{
  return active_.load(std::memory_order_acquire) && ready_.load(std::memory_order_acquire);
}
bool ClientEventConnection::deliver(std::shared_ptr<const std::string> message,
                                    ClientDeliveryPriority priority)
{
  if (!active_.load(std::memory_order_acquire))
    return false;
  const bool accepted = delivery_.push(std::move(message), priority);
  writer_wake_.fetch_add(1, std::memory_order_release);
  writer_wake_.notify_one();
  return accepted;
}
void ClientEventConnection::enqueue(std::string_view message, Clock::time_point now)
{
  if (!active_.load(std::memory_order_acquire) || !ready_.load(std::memory_order_acquire))
    return;
  if (auto* delivery = outbound_.try_reserve_write())
  {
    *delivery = {std::string(message), now + std::chrono::seconds(2)};
    outbound_.commit_write();
    writer_wake_.fetch_add(1, std::memory_order_release);
    writer_wake_.notify_one();
  }
}
bool ClientEventConnection::write_message(std::string_view message)
{
  if (!active_.load(std::memory_order_acquire) || writer_(message))
    return true;
  revoke();
  failure_(ClientDeliveryFailure::transport);
  return false;
}

void ClientEventConnection::write_loop() noexcept
{
  try
  {
    while (true)
    {
      const auto observed = writer_wake_.load(std::memory_order_acquire);
      if (stopping_.load(std::memory_order_acquire))
        return;
      if (revocation_close_.load(std::memory_order_acquire))
      {
        failure_(ClientDeliveryFailure::revoked);
        return;
      }
      if (!ready())
      {
        writer_wake_.wait(observed, std::memory_order_acquire);
        continue;
      }
      auto message = delivery_.pop();
      if (delivery_.failed())
      {
        revoke();
        failure_(ClientDeliveryFailure::overflow);
        return;
      }
      if (message)
      {
        if (!write_message(*message))
          return;
        continue;
      }
      const auto* delivery = outbound_.try_begin_read();
      if (!delivery)
      {
        writer_wake_.wait(observed, std::memory_order_acquire);
        continue;
      }
      if (Clock::now() < delivery->expires_at && !write_message(delivery->message))
        return;
      outbound_.commit_read();
    }
  }
  catch (...)
  {
    revoke();
    std::fputs("Client event writer failed\n", stderr);
  }
}
ClientEventRelay::ClientEventRelay(std::function<void(std::uint64_t)> retired,
                                   std::function<void(Clock::time_point)> maintenance)
    : retired_(std::move(retired)), maintenance_(std::move(maintenance)),
      relay_thread_([this] { relay_loop(); })
{
}
ClientEventRelay::~ClientEventRelay()
{
  for (auto& slot : slots_)
    if (slot.state.load(std::memory_order_acquire) == SlotState::active)
      disconnect(slot.connection.get());
  stopping_.store(true, std::memory_order_release);
  relay_wake_.fetch_add(1, std::memory_order_release);
  relay_wake_.notify_one();
  relay_thread_.join();
}
ClientEventConnection* ClientEventRelay::connect(ClientEventOrigin origin,
                                                 std::uint32_t permissions,
                                                 std::shared_ptr<ClientEventAuthority> authority,
                                                 std::function<bool(std::string_view)> writer,
                                                 std::function<void(ClientDeliveryFailure)> failure)
{
  if (!writer || !failure || !authority || origin.connection_id == 0 || origin.device_id.empty() ||
      origin.provider.empty() || origin.adapter_scope_id.empty())
    throw std::invalid_argument("Incomplete client event subscription");
  for (std::size_t index = 0; index < slots_.size(); ++index)
  {
    auto& slot = slots_[index];
    auto expected = SlotState::empty;
    if (!slot.state.compare_exchange_strong(expected, SlotState::reserved))
      continue;
    try
    {
      slot.connection.reset(new ClientEventConnection(std::move(origin),
                                                      permissions,
                                                      std::move(authority),
                                                      index,
                                                      relay_wake_,
                                                      std::move(writer),
                                                      std::move(failure)));
      slot.state.store(SlotState::active, std::memory_order_release);
      return slot.connection.get();
    }
    catch (...)
    {
      slot.state.store(SlotState::empty, std::memory_order_release);
      throw;
    }
  }
  return nullptr;
}
void ClientEventRelay::disconnect(ClientEventConnection* connection)
{
  if (!connection)
    return;
  auto& slot = slots_[connection->slot_index_];
  connection->revoke();
  slot.state.store(SlotState::retiring, std::memory_order_release);
  relay_wake_.fetch_add(1, std::memory_order_release);
  relay_wake_.notify_one();
  while (slot.state.load(std::memory_order_acquire) != SlotState::released)
    slot.state.wait(SlotState::retiring, std::memory_order_acquire);
  connection->stopping_.store(true, std::memory_order_release);
  connection->writer_wake_.fetch_add(1, std::memory_order_release);
  connection->writer_wake_.notify_one();
  connection->writer_thread_.join();
  slot.connection.reset();
  slot.state.store(SlotState::empty, std::memory_order_release);
}
bool ClientEventRelay::dispatch(ClientEventConnection& sender, Clock::time_point now)
{
  // One item per connection per pass prevents a noisy sender starving other connections.
  const auto* pending = sender.inbound_.try_begin_read();
  if (!pending)
    return false;
  if (sender.active_.load(std::memory_order_acquire) &&
      now - pending->received_at < std::chrono::seconds(2))
  {
    const auto publication = pending->notification(
        sender.origin_,
        sender.permissions_.load(std::memory_order_acquire) & sender.authority_->permissions(),
        now);
    if (publication.rejection)
      sender.enqueue(*publication.rejection, now);
    if (publication.broadcast)
      for (auto& slot : slots_)
        if (slot.state.load(std::memory_order_acquire) == SlotState::active)
        {
          auto& receiver = *slot.connection;
          if (receiver.origin_.adapter_scope_id == sender.origin_.adapter_scope_id &&
              receiver.origin_.provider == sender.origin_.provider)
            receiver.enqueue(*publication.broadcast, now);
        }
  }
  sender.inbound_.commit_read();
  return true;
}
bool ClientEventRelay::publish_local(std::vector<LocalClientEventDelivery> deliveries)
{
  if (deliveries.empty() || deliveries.size() > slots_.size() ||
      std::ranges::any_of(
          deliveries,
          [](const auto& delivery)
          { return delivery.connection_id == 0 || delivery.message.size() > 16 * 1024; }))
    return false;
  for (auto& publication : local_publications_)
  {
    auto expected = SlotState::empty;
    if (!publication.state.compare_exchange_strong(expected, SlotState::reserved))
      continue;
    publication.deliveries = std::move(deliveries);
    publication.expires_at = Clock::now() + std::chrono::seconds(2);
    publication.state.store(SlotState::active, std::memory_order_release);
    relay_wake_.fetch_add(1, std::memory_order_release);
    relay_wake_.notify_one();
    return true;
  }
  return false;
}

bool ClientEventRelay::dispatch_local(Clock::time_point now)
{
  bool processed = false;
  for (auto& publication : local_publications_)
  {
    if (publication.state.load(std::memory_order_acquire) != SlotState::active)
      continue;
    // Always release admission, including an allocation failure while copying to an egress.
    try
    {
      deliver_local(publication, now);
    }
    catch (...)
    {
      std::fputs("Local client event delivery failed\n", stderr);
    }
    publication.state.store(SlotState::empty, std::memory_order_release);
    processed = true;
  }
  return processed;
}

void ClientEventRelay::deliver_local(const LocalPublication& publication, Clock::time_point now)
{
  if (now >= publication.expires_at)
    return;
  for (auto& slot : slots_)
  {
    if (slot.state.load(std::memory_order_acquire) != SlotState::active)
      continue;
    const auto delivery = std::ranges::find(publication.deliveries,
                                            slot.connection->origin_.connection_id,
                                            &LocalClientEventDelivery::connection_id);
    if (delivery != publication.deliveries.end())
      slot.connection->enqueue(delivery->message, now);
  }
}

void ClientEventRelay::relay_loop() noexcept
{
  while (true)
  {
    const auto observed = relay_wake_.load(std::memory_order_acquire);
    if (stopping_.load(std::memory_order_acquire))
      return;
    const auto now = Clock::now();
    try
    {
      if (maintenance_)
        maintenance_(now);
    }
    catch (...)
    {
      std::fputs("Client event maintenance failed\n", stderr);
    }
    bool processed = dispatch_local(now);
    for (auto& slot : slots_)
    {
      const auto state = slot.state.load(std::memory_order_acquire);
      try
      {
        if (state == SlotState::active)
          processed = dispatch(*slot.connection, now) || processed;
        else if (state == SlotState::retiring)
        {
          if (retired_)
            retired_(slot.connection->origin_.connection_id);
          slot.state.store(SlotState::released, std::memory_order_release);
          slot.state.notify_one();
        }
      }
      catch (...)
      {
        std::fputs("Client event relay failed\n", stderr);
        slot.connection->revoke();
        if (state == SlotState::retiring)
        {
          slot.state.store(SlotState::released, std::memory_order_release);
          slot.state.notify_one();
        }
        else if (slot.connection->inbound_.try_begin_read())
          slot.connection->inbound_.commit_read();
      }
    }
    if (!processed)
      relay_wake_.wait(observed, std::memory_order_acquire);
  }
}
} // namespace studio_remote
