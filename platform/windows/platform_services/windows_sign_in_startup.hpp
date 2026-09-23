#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace studio_remote::windows
{

std::wstring build_sign_in_startup_command(const std::filesystem::path& executable,
                                           std::string_view provider_ids);

class WindowsSignInStartup
{
public:
  explicit WindowsSignInStartup(std::string provider_ids);

  bool enabled() const;
  void set_enabled(bool enabled) const;

private:
  std::wstring command_;
};

} // namespace studio_remote::windows
