#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace studio_remote
{
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

inline uint64_t monotonic_ns()
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
          .count());
}

enum class Role
{
  viewer = 0,
  operator_ = 1,
  admin = 2
};

inline std::string_view role_name(Role role)
{
  switch (role)
  {
  case Role::viewer:
    return "viewer";
  case Role::operator_:
    return "operator";
  case Role::admin:
    return "admin";
  }
  return "viewer";
}

inline Role parse_role(std::string_view value)
{
  if (value == "admin")
    return Role::admin;
  if (value == "operator")
    return Role::operator_;
  return Role::viewer;
}
} // namespace studio_remote
