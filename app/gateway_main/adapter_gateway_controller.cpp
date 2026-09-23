#include "adapter_gateway_controller.hpp"

#include <stdexcept>
#include <utility>

namespace studio_remote
{
AdapterGatewayController::AdapterGatewayController(std::vector<AdapterGatewayBinding> bindings)
    : bindings_(std::move(bindings))
{
  if (bindings_.empty())
    throw std::invalid_argument("At least one adapter gateway binding is required");
  for (const auto& gateway : bindings_)
    if (gateway.provider_id.empty() || !gateway.running || !gateway.start_runtime ||
        !gateway.stop_runtime || !gateway.start_media || !gateway.stop_media)
      throw std::invalid_argument("Adapter gateway bindings must be complete");
}

void AdapterGatewayController::start(std::string_view provider_id)
{
  std::lock_guard lock(mutex_);
  start_locked(binding(provider_id));
}

void AdapterGatewayController::stop(std::string_view provider_id)
{
  std::lock_guard lock(mutex_);
  stop_locked(binding(provider_id));
}

void AdapterGatewayController::restart(std::string_view provider_id)
{
  std::lock_guard lock(mutex_);
  auto& gateway = binding(provider_id);
  stop_locked(gateway);
  start_locked(gateway);
}

AdapterGatewayBinding& AdapterGatewayController::binding(std::string_view provider_id)
{
  for (auto& gateway : bindings_)
    if (gateway.provider_id == provider_id)
      return gateway;
  throw std::invalid_argument("The adapter gateway is not installed");
}

void AdapterGatewayController::start_locked(AdapterGatewayBinding& gateway)
{
  if (gateway.running())
    return;
  gateway.start_runtime();
  try
  {
    gateway.start_media();
  }
  catch (...)
  {
    gateway.stop_media();
    gateway.stop_runtime();
    throw;
  }
}

void AdapterGatewayController::stop_locked(AdapterGatewayBinding& gateway)
{
  if (!gateway.running())
    return;
  try
  {
    gateway.stop_media();
  }
  catch (...)
  {
    gateway.stop_runtime();
    throw;
  }
  gateway.stop_runtime();
}
} // namespace studio_remote
