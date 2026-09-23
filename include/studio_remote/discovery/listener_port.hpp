#pragma once

#include <cstdint>
#include <string>

namespace studio_remote
{
struct ListenerPortAvailability
{
  bool available = false;
  std::uint32_t error_code = 0;
  std::string warning;
};
} // namespace studio_remote
