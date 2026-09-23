#pragma once

#include <studio_remote/runtime/platform_services.hpp>

namespace studio_remote::macos
{
/// Resolves per-user native locations without creating directories or accessing credentials.
/// Instances are immutable and may be used by gateway workers independently of the UI.
class MacosPlatformServices final : public IPlatformServices
{
public:
  std::filesystem::path data_directory() const override;
  std::string platform_name() const override;
  std::chrono::nanoseconds monotonic_now() const override;

  std::filesystem::path cache_directory() const;
  std::filesystem::path log_directory() const;
  std::filesystem::path temporary_directory() const;
};
} // namespace studio_remote::macos
