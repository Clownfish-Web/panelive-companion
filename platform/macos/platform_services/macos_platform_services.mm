#include "macos_platform_services.hpp"

#import <Foundation/Foundation.h>

#include <stdexcept>

namespace studio_remote::macos
{
namespace
{
constexpr auto product_directory = "panelive-companion";

std::filesystem::path filesystem_path(NSURL* url)
{
  if (!url || !url.fileURL || !url.fileSystemRepresentation)
    throw std::runtime_error("Could not resolve macOS application directory");
  return std::filesystem::path(url.fileSystemRepresentation);
}

std::filesystem::path user_directory(NSSearchPathDirectory directory)
{
  return filesystem_path([[NSFileManager defaultManager] URLsForDirectory:directory
                                                                inDomains:NSUserDomainMask]
                             .firstObject);
}
} // namespace

std::filesystem::path MacosPlatformServices::data_directory() const
{
  @autoreleasepool
  {
    return user_directory(NSApplicationSupportDirectory) / product_directory;
  }
}

std::filesystem::path MacosPlatformServices::cache_directory() const
{
  @autoreleasepool
  {
    return user_directory(NSCachesDirectory) / product_directory;
  }
}

std::filesystem::path MacosPlatformServices::log_directory() const
{
  @autoreleasepool
  {
    return user_directory(NSLibraryDirectory) / "Logs" / product_directory;
  }
}

std::filesystem::path MacosPlatformServices::temporary_directory() const
{
  @autoreleasepool
  {
    return filesystem_path([NSFileManager defaultManager].temporaryDirectory) / product_directory;
  }
}

std::string MacosPlatformServices::platform_name() const
{
  return "macOS";
}

std::chrono::nanoseconds MacosPlatformServices::monotonic_now() const
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch());
}
} // namespace studio_remote::macos
