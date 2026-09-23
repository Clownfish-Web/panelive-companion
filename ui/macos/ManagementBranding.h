#pragma once

#import <Foundation/Foundation.h>

@class NSImage;

/// Decodes the exact Windows logo artwork embedded in the native executable.
FOUNDATION_EXPORT NSImage* _Nullable PLManagementBrandImage(void);
