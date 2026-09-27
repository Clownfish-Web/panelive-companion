#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace studio_remote::windows
{
// Pins the identity directory during loading/creation. Files are restricted to the process user
// and SYSTEM. Rejects reparse-point leaves and multiply linked files; never overwrites identities.
class PrivateTlsStorage
{
public:
  explicit PrivateTlsStorage(const std::filesystem::path& directory);
  ~PrivateTlsStorage();

  std::string read(const std::filesystem::path& path) const;
  void write_new(const std::filesystem::path& path, std::string_view bytes) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::windows
