#include "http_callback_lifetime.hpp"

#include <stdexcept>
#include <utility>

namespace studio_remote
{
HttpCallbackLifetime::Lease::Lease(HttpCallbackLifetime& owner) : owner_(&owner)
{
}

HttpCallbackLifetime::Lease::Lease(Lease&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr))
{
}

HttpCallbackLifetime::Lease::~Lease()
{
  if (!owner_)
    return;
  std::lock_guard lock(owner_->mutex_);
  --owner_->active_;
  owner_->idle_.notify_all();
}

void HttpCallbackLifetime::open()
{
  std::lock_guard lock(mutex_);
  if (active_ != 0)
    throw std::logic_error("HTTP callbacks must finish before reopening their lifetime");
  accepting_ = true;
}

std::optional<HttpCallbackLifetime::Lease> HttpCallbackLifetime::enter()
{
  std::lock_guard lock(mutex_);
  if (!accepting_)
    return std::nullopt;
  ++active_;
  return Lease(*this);
}

void HttpCallbackLifetime::close()
{
  std::unique_lock lock(mutex_);
  accepting_ = false;
  idle_.wait(lock, [this] { return active_ == 0; });
}
} // namespace studio_remote
