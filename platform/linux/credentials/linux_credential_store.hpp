#pragma once

#include <studio_remote/credentials/credential_store.hpp>

namespace studio_remote::linux
{
class LinuxCredentialStore final : public ICredentialStore
{
public:
  explicit LinuxCredentialStore(std::string instance_id);

  std::string random_token(std::size_t bytes) override;
  std::string hash(std::string_view value) override;
  bool constant_time_equal(std::string_view left, std::string_view right) override;
  std::string uuid_v4() override;
  void put(std::string_view key, std::string_view value) override;
  std::string get(std::string_view key) override;

private:
  std::string instance_id_;
};
} // namespace studio_remote::linux
