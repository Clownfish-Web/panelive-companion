#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <studio_remote/events/client_delivery_queue.hpp>
#include <studio_remote/media/fixed_spsc_ring.hpp>
#include <thread>
#include <vector>

namespace studio_remote
{
struct ClientEventOrigin
{
  std::uint64_t connection_id = 0;
  std::string device_id;
  std::string provider;
  std::string adapter_scope_id;
  bool operator==(const ClientEventOrigin&) const = default;
};
struct ClientEventPublication
{
  std::optional<std::string> broadcast;
  std::optional<std::string> rejection;
};
struct LocalClientEventDelivery
{
  std::uint64_t connection_id = 0;
  std::string message;
};
enum class ClientDeliveryFailure
{
  transport,
  overflow,
  revoked
};
/// Captures a validated typed event at the protocol boundary. Runs on the relay thread.
using ClientEventNotification = std::function<ClientEventPublication(
    const ClientEventOrigin&, std::uint32_t, std::chrono::steady_clock::time_point)>;

/// Provider snapshots are updated by existing state/control callbacks, never by event admission.
class ClientEventAuthority
{
public:
  void replace(std::uint32_t permissions) noexcept
  {
    permissions_.store(permissions);
  }
  void block(bool blocked) noexcept
  {
    blocked_.store(blocked);
  }
  std::uint32_t permissions() const noexcept
  {
    return blocked_.load() ? 0 : permissions_.load();
  }

private:
  std::atomic<std::uint32_t> permissions_{0};
  std::atomic<bool> blocked_{false};
};

/// Native receive callback -> ingress SPSC -> relay -> egress SPSC -> dedicated native writer.
/// Connection metadata is immutable; authority and revocation use lock-free atomic snapshots.
class ClientEventConnection
{
public:
  using Clock = std::chrono::steady_clock;
  bool submit(ClientEventNotification notification);
  void set_permissions(std::uint32_t permissions) noexcept;
  void revoke() noexcept;
  /// Retire authority immediately and send the native revocation close on this writer.
  void request_revocation_close() noexcept;
  void activate() noexcept;
  const ClientEventOrigin& origin() const noexcept;
  bool ready() const noexcept;
  /// Concurrent local publishers; caller pins connection lifetime through native close.
  /// Bootstrap may enqueue, but the writer waits for activate before sending.
  bool deliver(std::shared_ptr<const std::string> message, ClientDeliveryPriority priority);

private:
  friend class ClientEventRelay;
  struct Pending
  {
    ClientEventNotification notification;
    Clock::time_point received_at;
  };
  struct Delivery
  {
    std::string message;
    Clock::time_point expires_at;
  };
  ClientEventConnection(ClientEventOrigin origin,
                        std::uint32_t permissions,
                        std::shared_ptr<ClientEventAuthority> authority,
                        std::size_t slot_index,
                        std::atomic<std::uint32_t>& relay_wake,
                        std::function<bool(std::string_view)> writer,
                        std::function<void(ClientDeliveryFailure)> failure);
  void write_loop() noexcept;
  bool write_message(std::string_view message);
  void enqueue(std::string_view message, Clock::time_point now);
  ClientEventOrigin origin_;
  std::atomic<std::uint32_t> permissions_;
  std::atomic<bool> active_{true};
  std::atomic<bool> ready_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> revocation_close_{false};
  FixedSpscRing<Pending, 64> inbound_;
  FixedSpscRing<Delivery, 64> outbound_;
  ClientDeliveryQueue delivery_;
  // Provider and its connections share this immutable authority owner through writer retirement.
  std::shared_ptr<ClientEventAuthority> authority_;
  std::size_t slot_index_;
  std::atomic<std::uint32_t>& relay_wake_;
  std::atomic<std::uint32_t> writer_wake_{0};
  std::function<bool(std::string_view)> writer_;
  std::function<void(ClientDeliveryFailure)> failure_;
  std::thread writer_thread_;
};

/// Dedicated event lane. No application mutexes on admission, routing or dequeue.
/// Fixed atomic slots keep connections alive until the relay acknowledges retirement.
/// Only a connection's native close callback may disconnect it; shutdown drains those callbacks.
class ClientEventRelay
{
public:
  using Clock = std::chrono::steady_clock;
  explicit ClientEventRelay(std::function<void(std::uint64_t)> retired = {},
                            std::function<void(Clock::time_point)> maintenance = {});
  ~ClientEventRelay();
  ClientEventConnection* connect(ClientEventOrigin origin,
                                 std::uint32_t permissions,
                                 std::shared_ptr<ClientEventAuthority> authority,
                                 std::function<bool(std::string_view)> writer,
                                 std::function<void(ClientDeliveryFailure)> failure);
  void disconnect(ClientEventConnection* connection);
  /// Local publishers snapshot connection IDs, never device IDs, so reconnect cannot replay work.
  /// Concurrent callers are supported; full admission drops the new best-effort notification.
  bool publish_local(std::vector<LocalClientEventDelivery> deliveries);

private:
  enum class SlotState : std::uint32_t
  {
    empty,
    reserved,
    active,
    retiring,
    released
  };
  struct Slot
  {
    std::atomic<SlotState> state{SlotState::empty};
    std::unique_ptr<ClientEventConnection> connection;
  };
  void relay_loop() noexcept;
  bool dispatch(ClientEventConnection& sender, Clock::time_point now);
  bool dispatch_local(Clock::time_point now);
  struct LocalPublication
  {
    std::atomic<SlotState> state{SlotState::empty};
    std::vector<LocalClientEventDelivery> deliveries;
    Clock::time_point expires_at;
  };
  void deliver_local(const LocalPublication& publication, Clock::time_point now);
  std::array<LocalPublication, 64> local_publications_;
  std::array<Slot, 64> slots_;
  std::function<void(std::uint64_t)> retired_;
  std::function<void(Clock::time_point)> maintenance_;
  std::atomic<bool> stopping_{false};
  std::atomic<std::uint32_t> relay_wake_{0};
  std::thread relay_thread_;
};
} // namespace studio_remote
