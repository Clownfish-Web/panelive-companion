#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
/// Immutable RGBA pixels for an active single-use code. Never log or persist this value.
@interface PLPairingCode : NSObject
@property(nonatomic, readonly, copy) NSData* pixels;
@property(nonatomic, readonly) NSInteger size;
@property(nonatomic, readonly) uint64_t expiresUnixSeconds;
/// Resamples the shared pixels for native display. Returns nil if rendering fails.
- (nullable NSData*)pixelsForSize:(NSInteger)size;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end
NS_ASSUME_NONNULL_END
