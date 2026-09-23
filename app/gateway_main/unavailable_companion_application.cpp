#include "platform_companion_application.hpp"

#include <iostream>
#include <memory>

namespace studio_remote
{
namespace
{
class UnavailableCompanionApplication final : public PlatformCompanionApplication
{
public:
  int run(int argc, char** argv) override
  {
    static_cast<void>(argc);
    static_cast<void>(argv);
    std::cerr << "This source tree builds portable core targets on this platform, but its "
                 "production composition adapter is not enabled yet.\n";
    return 2;
  }
};
} // namespace

std::unique_ptr<PlatformCompanionApplication> create_platform_companion_application()
{
  return std::make_unique<UnavailableCompanionApplication>();
}
} // namespace studio_remote
