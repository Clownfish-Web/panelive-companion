#pragma once

#include <stop_token>

namespace studio_remote::tally
{
enum class ServiceHealth
{
  unchecked,
  healthy,
  unavailable,
  unhealthy,
  invalid_response
};

class IServiceHealthProbe
{
public:
  virtual ~IServiceHealthProbe() = default;
  // One bounded HTTPS request, cancellable by the owning application. No implicit retries.
  virtual ServiceHealth probe(std::stop_token stop) = 0;
};
} // namespace studio_remote::tally
