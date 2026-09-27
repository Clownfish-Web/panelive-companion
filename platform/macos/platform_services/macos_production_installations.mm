#include "macos_production_installations.hpp"

#import <Foundation/Foundation.h>

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>

namespace studio_remote::macos
{
namespace
{
std::filesystem::path user_directory(NSSearchPathDirectory directory)
{
  @autoreleasepool
  {
    NSURL* location = [[NSFileManager defaultManager] URLsForDirectory:directory
                                                             inDomains:NSUserDomainMask]
                          .firstObject;
    if (!location.fileSystemRepresentation)
      throw std::runtime_error("Could not resolve the current user's installation directory");
    return location.fileSystemRepresentation;
  }
}
} // namespace

std::filesystem::path companion_installation_directory()
{
  return "/Applications/panelive-companion.app";
}

std::filesystem::path installation_record_file()
{
  return user_directory(NSLibraryDirectory) / "Preferences/panelive-companion.plist";
}

std::filesystem::path installed_obs_directory()
{
  return installed_obs_directory(installation_record_file());
}

std::filesystem::path installed_obs_directory(const std::filesystem::path& record_file)
{
  return recorded_obs_directory(record_file).lexically_normal();
}

std::filesystem::path recorded_obs_directory(const std::filesystem::path& record_file)
{
  @autoreleasepool
  {
    // Unlike a registry string query, reading a plist loads a document. Bound that native work.
    constexpr std::size_t maximum_record_bytes = 64 * 1024;
    std::array<char, maximum_record_bytes + 1> bytes{};
    std::ifstream input(record_file, std::ios::binary);
    if (!input)
      return {};
    input.read(bytes.data(), bytes.size());
    if (input.bad() || !input.eof() || input.gcount() == 0)
      return {};
    NSData* data = [NSData dataWithBytes:bytes.data() length:input.gcount()];
    id record = [NSPropertyListSerialization propertyListWithData:data
                                                          options:NSPropertyListImmutable
                                                           format:nullptr
                                                            error:nullptr];
    if (![record isKindOfClass:NSDictionary.class])
      return {};
    id value = record[@"ObsDirectory"];
    if (![value isKindOfClass:NSString.class])
      return {};
    NSData* encoded = [value dataUsingEncoding:NSUTF8StringEncoding allowLossyConversion:NO];
    if (!encoded || encoded.length == 0)
      return {};
    std::string path(static_cast<const char*>(encoded.bytes), encoded.length);
    if (path.find('\0') != std::string::npos)
      return {};
    return std::filesystem::path(path);
  }
}
} // namespace studio_remote::macos
