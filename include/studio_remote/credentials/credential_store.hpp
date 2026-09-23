#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace studio_remote
{
struct GatewayIdentity
{
  std::string instance_id;
  std::string name;
  std::string certificate_fingerprint;
};

class ICredentialStore
{
public:
  virtual ~ICredentialStore() = default;
  virtual std::string random_token(std::size_t bytes) = 0;
  virtual std::string hash(std::string_view value) = 0;
  virtual bool constant_time_equal(std::string_view left, std::string_view right) = 0;
  virtual std::string uuid_v4() = 0;
  virtual void put(std::string_view key, std::string_view value) = 0;
  virtual std::string get(std::string_view key) = 0;
};
} // namespace studio_remote
