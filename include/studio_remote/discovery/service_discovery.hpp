#pragma once
#include <cstdint>
#include <map>
#include <string>
namespace studio_remote
{
struct ServiceRecord
{
  std::string instance_name;
  std::string service_type;
  std::uint16_t port = 0;
  std::map<std::string, std::string> txt;
};
class IServiceDiscovery
{
public:
  virtual ~IServiceDiscovery() = default;
  virtual void advertise(const ServiceRecord&) = 0;
  virtual void stop() = 0;
};
} // namespace studio_remote
