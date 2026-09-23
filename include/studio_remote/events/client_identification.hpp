#pragma once

#include <string>

namespace studio_remote
{
/// Visual identification is mandatory and deliberately has no configurable flag.
struct ClientIdentificationOptions
{
  bool audible = true;
  bool vibrate = true;
  bool operator==(const ClientIdentificationOptions&) const = default;
};

/// Temporary local display label shared by management and that device's identify notifications.
struct ClientIdentification
{
  std::string device_id;
  std::string code;
};
} // namespace studio_remote
