#include <studio_remote/obs/obs_pending_requests.hpp>

#include <stdexcept>
#include <utility>

namespace studio_remote
{
bool ObsPendingRequests::full() const
{
  std::lock_guard lock(mutex_);
  return pending_.size() >= maximum_pending_requests;
}

bool ObsPendingRequests::contains(std::string_view id) const
{
  std::lock_guard lock(mutex_);
  return pending_.contains(std::string(id));
}

std::future<json> ObsPendingRequests::register_request(std::string id)
{
  std::lock_guard lock(mutex_);
  if (pending_.size() >= maximum_pending_requests)
    throw std::runtime_error("OBS request queue is full");
  return pending_.try_emplace(std::move(id)).first->second.get_future();
}

void ObsPendingRequests::erase(std::string_view id)
{
  // Abandonment wakes the future too; destroy its promise after releasing the table mutex.
  static_cast<void>(take(id));
}

std::optional<std::promise<json>> ObsPendingRequests::take(std::string_view id)
{
  std::lock_guard lock(mutex_);
  const auto found = pending_.find(std::string(id));
  if (found == pending_.end())
    return std::nullopt;
  std::optional<std::promise<json>> completion(std::move(found->second));
  pending_.erase(found);
  return completion;
}

void ObsPendingRequests::resolve(std::string_view id, json response)
{
  if (auto completion = take(id))
    completion->set_value(std::move(response));
}

void ObsPendingRequests::reject(std::string_view id, std::exception_ptr failure)
{
  if (!failure)
    throw std::invalid_argument("OBS request rejection requires an error");
  if (auto completion = take(id))
    completion->set_exception(std::move(failure));
}

void ObsPendingRequests::cancel_all(std::exception_ptr failure)
{
  if (!failure)
    throw std::invalid_argument("OBS request cancellation requires an error");
  decltype(pending_) cancelled;
  {
    std::lock_guard lock(mutex_);
    cancelled.swap(pending_);
  }
  for (auto& [id, completion] : cancelled)
    completion.set_exception(failure);
}
} // namespace studio_remote
