#include "macos_obs_quick_transition_catalog.hpp"

#import <Foundation/Foundation.h>

namespace studio_remote::macos
{
std::filesystem::path obs_configuration_root()
{
  @autoreleasepool
  {
    NSURL* application_support =
        [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                               inDomains:NSUserDomainMask]
            .firstObject;
    if (!application_support || !application_support.fileURL ||
        !application_support.fileSystemRepresentation)
      return {};
    return std::filesystem::path(application_support.fileSystemRepresentation) / "obs-studio";
  }
}
} // namespace studio_remote::macos
