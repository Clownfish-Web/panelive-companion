#pragma once

#import <Foundation/Foundation.h>

#include <stdexcept>
#include <string_view>

namespace studio_remote::macos
{
inline NSString* native_text(std::string_view value)
{
  NSString* result = [[NSString alloc] initWithBytes:value.data()
                                              length:value.size()
                                            encoding:NSUTF8StringEncoding];
  if (!result)
    throw std::invalid_argument("Management presentation contains invalid UTF-8");
  return result;
}
} // namespace studio_remote::macos
