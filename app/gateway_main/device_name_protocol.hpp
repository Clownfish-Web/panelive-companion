#pragma once

#include <studio_remote/common/types.hpp>

namespace studio_remote
{
inline json device_renamed_response(std::string_view device_id, std::string_view name)
{
  return {{"deviceId", device_id}, {"name", name}};
}
} // namespace studio_remote
