#pragma once
#include <filesystem>
#include <studio_remote/credentials/credential_store.hpp>
namespace studio_remote::windows
{
class WindowsCredentialStore final : public ICredentialStore
{
public:
  // Owner-DPAPI secrets, at most 64 KiB, under lowercase file-safe references. Only a missing
  // leaf file reads as empty. Successful writes flush and replace; failures preserve old data.
  explicit WindowsCredentialStore(std::filesystem::path directory);
  std::string random_token(std::size_t bytes) override;
  std::string hash(std::string_view value) override;
  bool constant_time_equal(std::string_view left, std::string_view right) override;
  std::string uuid_v4() override;
  void put(std::string_view key, std::string_view value) override;
  std::string get(std::string_view key) override;

private:
  std::filesystem::path directory_;
};
} // namespace studio_remote::windows
