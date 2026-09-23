#pragma once
#include <chrono>
#include <filesystem>
#include <string>
namespace studio_remote
{
class IPlatformServices
{
public:
  virtual ~IPlatformServices() = default;
  virtual std::filesystem::path data_directory() const = 0;
  virtual std::string platform_name() const = 0;
  virtual std::chrono::nanoseconds monotonic_now() const = 0;
};
} // namespace studio_remote
