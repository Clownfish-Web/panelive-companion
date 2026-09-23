#pragma once

#include <studio_remote/tally/service_health.hpp>

#include <chrono>
#include <string>

namespace studio_remote::windows
{
class WindowsTallyHealthProbe final : public tally::IServiceHealthProbe
{
public:
  explicit WindowsTallyHealthProbe(std::string url,
                                   std::chrono::milliseconds timeout = std::chrono::seconds(4));
  tally::ServiceHealth probe(std::stop_token stop) override;

private:
  const std::string url_;
  const std::chrono::milliseconds timeout_;
};
} // namespace studio_remote::windows
