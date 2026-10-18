#include "ManagementBranding.h"

#include "companion_tray_artwork.hpp"
#include "client_stores.hpp"

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

NSURL* PLManagementClientAppStoreURL(void)
{
  const auto* url = studio_remote::client_stores::app_store_url;
  return *url ? [NSURL URLWithString:[NSString stringWithUTF8String:url]] : nil;
}

NSURL* PLManagementClientPlayStoreURL(void)
{
  const auto* url = studio_remote::client_stores::play_store_url;
  return *url ? [NSURL URLWithString:[NSString stringWithUTF8String:url]] : nil;
}
