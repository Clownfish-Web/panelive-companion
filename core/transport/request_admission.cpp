#include <studio_remote/transport/request_admission.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace studio_remote
{
namespace
{
constexpr auto principal_retention = std::chrono::minutes(5);
constexpr std::uint32_t capacity_retry_ms = 250;

struct TokenBucket
{
  double tokens = 0;
  RequestAdmission::Clock::time_point updated;
  bool initialized = false;

  std::uint32_t retry_after(RequestAllowance allowance, RequestAdmission::Clock::time_point now)
  {
    if (!initialized)
    {
      tokens = allowance.burst;
      updated = now;
      initialized = true;
    }
    const auto elapsed = std::chrono::duration<double>(now - updated).count();
    if (elapsed > 0)
    {
      tokens = std::min<double>(allowance.burst, tokens + elapsed * allowance.refill_per_second);
      updated = now;
    }
    if (tokens >= 1)
      return 0;
    return std::max(
        1U,
        static_cast<std::uint32_t>(std::ceil((1 - tokens) * 1000 / allowance.refill_per_second)));
  }
};

struct Principal
{
  std::array<TokenBucket, static_cast<std::size_t>(RequestScope::count)> buckets;
  RequestAdmission::Clock::time_point last_used;
  std::array<std::size_t, static_cast<std::size_t>(RequestConcurrencyPool::count)> active{};
};
using Principals = std::unordered_map<std::string, Principal>;

Principal* find_principal(Principals& principals,
                          const std::string& key,
                          std::size_t capacity,
                          RequestAdmission::Clock::time_point now)
{
  if (const auto found = principals.find(key); found != principals.end())
  {
    found->second.last_used = std::max(found->second.last_used, now);
    return &found->second;
  }
  if (principals.size() >= capacity)
  {
    std::erase_if(principals,
                  [&](const auto& entry)
                  {
                    return std::ranges::all_of(entry.second.active,
                                               [](auto count) { return count == 0; }) &&
                           now - entry.second.last_used >= principal_retention;
                  });
    if (principals.size() >= capacity)
      return nullptr;
  }
  auto& principal = principals[key];
  principal.last_used = now;
  return &principal;
}
} // namespace

RequestAllowance request_allowance(RequestScope scope)
{
  switch (scope)
  {
  case RequestScope::identity:
    return {2, 20, RequestConcurrencyPool::unrestricted};
  case RequestScope::pairing:
    return {8.0 / 60, 8, RequestConcurrencyPool::unrestricted};
  case RequestScope::authentication:
    return {2, 20, RequestConcurrencyPool::unrestricted};
  case RequestScope::websocket:
    return {2, 10, RequestConcurrencyPool::unrestricted};
  case RequestScope::critical_control:
    return {20, 40, RequestConcurrencyPool::unrestricted};
  case RequestScope::control:
    return {20, 40, RequestConcurrencyPool::unrestricted};
  case RequestScope::state:
    return {40, 80, RequestConcurrencyPool::unrestricted};
  case RequestScope::catalog:
    return {5, 20, RequestConcurrencyPool::background};
  case RequestScope::teleprompter:
    return {60, 120, RequestConcurrencyPool::teleprompter};
  case RequestScope::remote_tally:
    return {5, 20, RequestConcurrencyPool::background};
  case RequestScope::thumbnail:
    return {2, 8, RequestConcurrencyPool::background};
  case RequestScope::hotkey:
    return {1, 10, RequestConcurrencyPool::unrestricted};
  case RequestScope::media_offer:
    return {2, 4, RequestConcurrencyPool::background};
  case RequestScope::media_recovery:
    return {2, 4, RequestConcurrencyPool::unrestricted};
  case RequestScope::administration:
    return {1, 10, RequestConcurrencyPool::background};
  case RequestScope::device_management:
    return {1, 10, RequestConcurrencyPool::unrestricted};
  case RequestScope::invalid_request:
    return {1, 10, RequestConcurrencyPool::unrestricted};
  case RequestScope::count:
    break;
  }
  throw std::invalid_argument("Invalid request admission scope");
}

std::string_view request_scope_name(RequestScope scope)
{
  switch (scope)
  {
  case RequestScope::identity:
    return "identity";
  case RequestScope::pairing:
    return "pairing";
  case RequestScope::authentication:
    return "authentication";
  case RequestScope::websocket:
    return "websocket";
  case RequestScope::critical_control:
    return "critical-controls";
  case RequestScope::control:
    return "controls";
  case RequestScope::state:
    return "state";
  case RequestScope::catalog:
    return "catalogs";
  case RequestScope::teleprompter:
    return "teleprompter";
  case RequestScope::remote_tally:
    return "remote-tally";
  case RequestScope::thumbnail:
    return "thumbnails";
  case RequestScope::hotkey:
    return "hotkeys";
  case RequestScope::media_offer:
    return "webrtc";
  case RequestScope::media_recovery:
    return "webrtc-recovery";
  case RequestScope::administration:
    return "administrative";
  case RequestScope::device_management:
    return "device-management";
  case RequestScope::invalid_request:
    return "invalid-requests";
  case RequestScope::count:
    break;
  }
  throw std::invalid_argument("Invalid request admission scope");
}

struct RequestAdmission::Impl
{
  std::mutex mutex;
  Principals devices;
  Principals addresses;
  std::array<std::size_t, static_cast<std::size_t>(RequestConcurrencyPool::count)> active{};
};

RequestAdmission::RequestReservation::RequestReservation(RequestAdmission& owner,
                                                         std::string_view principal,
                                                         RequestConcurrencyPool pool)
    : owner_(owner), principal_(principal), pool_(pool)
{
}

RequestAdmission::RequestReservation::~RequestReservation()
{
  auto& admission = *owner_.impl_;
  std::lock_guard lock(admission.mutex);
  const auto index = static_cast<std::size_t>(pool_);
  --admission.devices.at(principal_).active[index];
  --admission.active[index];
}

RequestAdmission::RequestAdmission() : impl_(std::make_unique<Impl>())
{
}
RequestAdmission::~RequestAdmission() = default;

RequestAdmission::Result RequestAdmission::admit_device(std::string_view provider,
                                                        std::string_view device_id,
                                                        RequestScope scope,
                                                        Clock::time_point now)
{
  const auto allowance = request_allowance(scope);
  // Length-prefix the provider so arbitrary opaque identifiers cannot collide.
  const auto key =
      std::to_string(provider.size()) + ":" + std::string(provider) + std::string(device_id);
  std::lock_guard lock(impl_->mutex);
  auto* principal = find_principal(impl_->devices, key, maximum_authenticated_principals, now);
  if (!principal)
    return {capacity_retry_ms, {}};
  const auto pool_index = static_cast<std::size_t>(allowance.pool);
  const auto global_limit = allowance.pool == RequestConcurrencyPool::teleprompter
                                ? maximum_teleprompter_requests
                                : maximum_background_requests;
  const auto device_limit = allowance.pool == RequestConcurrencyPool::teleprompter
                                ? maximum_teleprompter_requests_per_device
                                : maximum_background_requests_per_device;
  if (allowance.pool != RequestConcurrencyPool::unrestricted &&
      (impl_->active[pool_index] >= global_limit || principal->active[pool_index] >= device_limit))
    return {capacity_retry_ms, {}};
  auto& bucket = principal->buckets[static_cast<std::size_t>(scope)];
  if (const auto retry = bucket.retry_after(allowance, now))
    return {retry, {}};
  Result result;
  if (allowance.pool != RequestConcurrencyPool::unrestricted)
  {
    result.reservation.reset(new RequestReservation(*this, key, allowance.pool));
    ++principal->active[pool_index];
    ++impl_->active[pool_index];
  }
  --bucket.tokens;
  return result;
}

RequestAdmission::Result
RequestAdmission::admit_public(std::string_view address, RequestScope scope, Clock::time_point now)
{
  if (scope != RequestScope::identity && scope != RequestScope::pairing &&
      scope != RequestScope::authentication)
    throw std::invalid_argument("Authenticated scope requires a device identity");
  const auto allowance = request_allowance(scope);
  std::lock_guard lock(impl_->mutex);
  auto* principal =
      find_principal(impl_->addresses, std::string(address), maximum_public_principals, now);
  if (!principal)
    return {capacity_retry_ms, {}};
  auto& bucket = principal->buckets[static_cast<std::size_t>(scope)];
  if (const auto retry = bucket.retry_after(allowance, now))
    return {retry, {}};
  --bucket.tokens;
  return {};
}
} // namespace studio_remote
