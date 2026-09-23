#pragma once

#include <cstddef>
#include <exception>
#include <future>
#include <mutex>
#include <optional>
#include <studio_remote/common/types.hpp>
#include <unordered_map>

namespace studio_remote
{
/// Client/session-owned OBS request correlation. Request workers register/remove IDs; the reader
/// resolves/rejects them; the lifecycle owner cancels before teardown. This table owns no threads,
/// transport or session phase. Every operation is thread-safe and invokes no client callback.
/// Promise ownership leaves the table under its mutex; fulfillment occurs after that mutex
/// releases.
class ObsPendingRequests final
{
public:
  static constexpr std::size_t maximum_pending_requests = 256;

  bool full() const;
  bool contains(std::string_view id) const;
  std::future<json> register_request(std::string id);
  void erase(std::string_view id);
  void resolve(std::string_view id, json response);
  void reject(std::string_view id, std::exception_ptr failure);
  void cancel_all(std::exception_ptr failure);

private:
  std::optional<std::promise<json>> take(std::string_view id);
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::promise<json>> pending_;
};
} // namespace studio_remote
