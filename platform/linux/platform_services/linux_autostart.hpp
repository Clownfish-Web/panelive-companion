#pragma once

#include <filesystem>

namespace studio_remote::linux
{
class SignInStartup
{
public:
  SignInStartup();
  bool enabled() const;
  void set_enabled(bool enabled) const;
  const std::filesystem::path& path() const noexcept;

private:
  std::filesystem::path path_;
};
} // namespace studio_remote::linux
