#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace studio_remote::macos
{
// Retains the identity directory descriptor and restricts files to the process
// user. Rejects symlink leaves and multiply linked files; never overwrites
// identities. Ancestors must be trusted.
class PrivateTlsStorage
{
public:
  explicit PrivateTlsStorage(const std::filesystem::path& directory);
  ~PrivateTlsStorage();

  // Only a missing file returns empty; corrupt or inaccessible storage is an
  // error.
  std::string read(const std::filesystem::path& path) const;
  void write_new(const std::filesystem::path& path, std::string_view bytes) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
