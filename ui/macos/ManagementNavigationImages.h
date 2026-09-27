#pragma once

#import <Foundation/Foundation.h>

@class NSImage;

/// Decodes the Linux Dispatch SVG for the next navigation action, as native template artwork.
FOUNDATION_EXPORT NSImage* _Nullable PLManagementNavigationToggleImage(BOOL collapsed);
