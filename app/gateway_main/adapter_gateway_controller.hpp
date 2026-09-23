#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{
struct AdapterGatewayBinding
{
  std::string provider_id;
  std::function<bool()> running;
  std::function<void()> start_runtime;
  std::function<void()> stop_runtime;
  std::function<void()> start_media;
  std::function<void()> stop_media;
};

class AdapterGatewayController
{
public:
  explicit AdapterGatewayController(std::vector<AdapterGatewayBinding> bindings);

  void start(std::string_view provider_id);
  void stop(std::string_view provider_id);
  void restart(std::string_view provider_id);

private:
  AdapterGatewayBinding& binding(std::string_view provider_id);
  void start_locked(AdapterGatewayBinding& gateway);
  void stop_locked(AdapterGatewayBinding& gateway);

  std::vector<AdapterGatewayBinding> bindings_;
  std::mutex mutex_;
};
} // namespace studio_remote
