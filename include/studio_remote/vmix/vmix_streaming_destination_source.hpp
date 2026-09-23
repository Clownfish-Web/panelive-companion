#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace studio_remote::vmix
{

struct StreamingDestinationConfiguration
{
  std::uint32_t stream = 0;
  std::string name_or_key;
  std::string host;
  std::string service_kind = "custom";
  bool operator==(const StreamingDestinationConfiguration&) const = default;
};

class IVmixStreamingDestinationSource
{
public:
  virtual ~IVmixStreamingDestinationSource() = default;
  virtual bool available() const = 0;

  // A value, including an empty vector, is an authoritative current roster. nullopt means that the
  // source could not be observed and the adapter must retain its fallback behavior.
  virtual std::optional<std::vector<StreamingDestinationConfiguration>> read_destinations() = 0;
};

} // namespace studio_remote::vmix
