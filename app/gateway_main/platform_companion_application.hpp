#pragma once

#include <memory>

namespace studio_remote
{
/// Platform composition boundary used by the single process entry point.
class PlatformCompanionApplication
{
public:
  virtual ~PlatformCompanionApplication() = default;

  virtual int run(int argc, char** argv) = 0;
};

/// Created by exactly one host-selected composition target.
std::unique_ptr<PlatformCompanionApplication> create_platform_companion_application();
} // namespace studio_remote
