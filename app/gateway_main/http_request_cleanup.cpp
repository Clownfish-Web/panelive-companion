#include "http_server.hpp"

#include <studio_remote/common/client_request_lifetime.hpp>

#include <algorithm>

namespace studio_remote
{
GatewayHttpServer::GenericCommandAdmission::~GenericCommandAdmission()
{
  if (admitted)
  {
    std::lock_guard lock(server.generic_command_cache_mutex_);
    server.generic_commands_in_flight_.erase(key);
  }
}

void GatewayHttpServer::expire_client_requests(Clock::time_point now)
{
  {
    std::lock_guard lock(generic_command_cache_mutex_);
    if (now < next_request_cleanup_)
      return;
    next_request_cleanup_ = now + std::chrono::seconds(1);
    std::erase_if(role_request_operations_,
                  [now](const auto& entry)
                  { return now - entry.second.admitted_at >= maximum_client_request_age; });
    std::erase_if(global_setting_request_operations_,
                  [now](const auto& entry)
                  { return now - entry.second.admitted_at >= maximum_client_request_age; });
    std::erase_if(generic_command_cache_,
                  [this, now](const auto& entry)
                  {
                    const bool pending = role_request_operations_.contains(entry.first) ||
                                         global_setting_request_operations_.contains(entry.first);
                    return !pending && now - entry.second.first >= std::chrono::minutes(10);
                  });
  }
}

void GatewayHttpServer::request_cleanup_loop()
{
  while (!delivery_stopping_)
  {
    expire_client_requests(Clock::now());
    std::unique_lock lock(maintenance_mutex_);
    maintenance_cv_.wait_for(
        lock, std::chrono::seconds(1), [this] { return delivery_stopping_.load(); });
  }
}
} // namespace studio_remote
