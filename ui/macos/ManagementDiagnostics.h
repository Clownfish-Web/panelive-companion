#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
typedef NS_ENUM(NSInteger, PLDiagnosticTap) { PLDiagnosticTapPreOpus, PLDiagnosticTapPostOpus };

@interface PLAudioOutput : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* name;
@property(nonatomic, readonly) BOOL isDefault;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

/// Immutable presentation of cached diagnostics. Reading it never starts capture or disk I/O.
@interface PLDiagnostics : NSObject
@property(nonatomic, readonly) BOOL enduranceEnabled;
@property(nonatomic, readonly, copy) NSString* enduranceSummary;
@property(nonatomic, readonly, copy) NSString* enduranceError;
@property(nonatomic, readonly, copy) NSArray<PLAudioOutput*>* outputs;
@property(nonatomic, readonly) BOOL playing;
@property(nonatomic, readonly) float gain;
@property(nonatomic, readonly) float maximumPeak;
@property(nonatomic, readonly) float averagePeak;
@property(nonatomic, readonly, copy) NSString* audioSummary;
@property(nonatomic, readonly) BOOL debugActive;
@property(nonatomic, readonly) BOOL debugAvailable;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end
NS_ASSUME_NONNULL_END
