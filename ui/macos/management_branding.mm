#include "ManagementBranding.h"

#include "companion_tray_artwork.hpp"

#import <AppKit/AppKit.h>

NSImage* PLManagementBrandImage(void)
{
  using namespace studio_remote::macos::artwork;
  NSData* bytes = [NSData dataWithBytes:companion_64 length:sizeof(companion_64)];
  NSImage* image = [[NSImage alloc] initWithData:bytes];
  image.size = NSMakeSize(32, 32);
  [image setTemplate:NO];
  return image;
}
