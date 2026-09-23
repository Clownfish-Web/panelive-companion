#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace studio_remote
{
enum class RequestScope
{
  identity,
  pairing,
  authentication,
  websocket,
  critical_control,
  control,
  state,
  catalog,
  teleprompter,
  remote_tally,
  thumbnail,
  hotkey,
  media_offer,
  media_recovery,
  administration,
  device_management,
  invalid_request,
  count
};

enum class RequestConcurrencyPool
{
  unrestricted,
  background,
  teleprompter,
  count
};

struct RequestAllowance
{
  double refill_per_second;
  std::uint32_t burst;
  RequestConcurrencyPool pool;
};

RequestAllowance request_allowance(RequestScope scope);
std::string_view request_scope_name(RequestScope scope);

/// Synchronized admission policy. Callers supply monotonic time; no callbacks or I/O run here.
/// Authenticated devices and public source addresses have separate, bounded accounting tables.
class RequestAdmission
{
public:
  using Clock = std::chrono::steady_clock;
  static constexpr std::size_t maximum_background_requests = 8;
  static constexpr std::size_t maximum_background_requests_per_device = 2;
  static constexpr std::size_t maximum_teleprompter_requests = 32;
  static constexpr std::size_t maximum_teleprompter_requests_per_device = 8;
  static constexpr std::size_t maximum_authenticated_principals = 1024;
  static constexpr std::size_t maximum_public_principals = 1024;

  /// Borrowed owner must outlive every reservation. HTTP shutdown joins callbacks first.
  class RequestReservation
  {
  public:
    ~RequestReservation();
    RequestReservation(const RequestReservation&) = delete;
    RequestReservation& operator=(const RequestReservation&) = delete;

  private:
    friend class RequestAdmission;
    RequestReservation(RequestAdmission& owner,
                       std::string_view principal,
                       RequestConcurrencyPool pool);
    RequestAdmission& owner_;
    std::string principal_;
    RequestConcurrencyPool pool_;
  };

  struct Result
  {
    // Zero means admitted. Rejection never queues a request or consumes a token.
    std::uint32_t retry_after_ms = 0;
    std::unique_ptr<RequestReservation> reservation;
  };

  RequestAdmission();
  ~RequestAdmission();
  RequestAdmission(const RequestAdmission&) = delete;
  RequestAdmission& operator=(const RequestAdmission&) = delete;

  Result admit_device(std::string_view provider,
                      std::string_view device_id,
                      RequestScope scope,
                      Clock::time_point now);
  Result admit_public(std::string_view address, RequestScope scope, Clock::time_point now);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote
