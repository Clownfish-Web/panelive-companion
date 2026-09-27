#include "ManagementNavigationImages.h"

#include "navigation_artwork.hpp"

#import <AppKit/AppKit.h>

NSImage* PLManagementNavigationToggleImage(BOOL collapsed)
{
  using namespace studio_remote::macos::artwork;
  NSData* bytes =
      collapsed ? [NSData dataWithBytes:navigation_expand length:sizeof(navigation_expand)]
                : [NSData dataWithBytes:navigation_collapse length:sizeof(navigation_collapse)];
  NSImage* image = [[NSImage alloc] initWithData:bytes];
  image.size = NSMakeSize(20, 20);
  [image setTemplate:YES];
  return image;
}
