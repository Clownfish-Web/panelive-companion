#pragma once

#include <studio_remote/common/types.hpp>

#include <string>

namespace studio_remote
{
struct RoleChangeRequest
{
  std::string request_id;
  std::string device_id;
  std::string device_name;
  Role requested_role = Role::viewer;
  std::string production_provider;
  std::string production_backend_name;
};

struct GlobalSettingChangeRequest
{
  std::string request_id;
  std::string device_id;
  std::string device_name;
  std::string setting_id;
  std::string setting_name;
  std::string warning;
  json requested_value;
};
} // namespace studio_remote
