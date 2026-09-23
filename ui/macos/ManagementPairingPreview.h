#pragma once

#import "ManagementPairing.h"

NS_ASSUME_NONNULL_BEGIN
/// Window-owned, main-thread facade over the shared presentation countdown.
NS_SWIFT_UI_ACTOR
@interface PLPairingPreview : NSObject
@property(class, nonatomic, readonly) NSInteger normalSize;
@property(class, nonatomic, readonly) NSInteger enlargedSize;
- (BOOL)openCode:(PLPairingCode*)code;
- (NSInteger)secondsRemainingForCode:(nullable PLPairingCode*)code;
- (void)close;
@end
NS_ASSUME_NONNULL_END
