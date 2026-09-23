#pragma once

#import "ManagementDiagnostics.h"
#import "ManagementPairing.h"
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, PLManagementPage) {
  PLManagementPageClients,
  PLManagementPageTeleprompter,
  PLManagementPageAdapters,
  PLManagementPageCompanionSettings,
  PLManagementPageStatus,
  PLManagementPageDiagnostics,
  PLManagementPageAdvancedSettings,
  PLManagementPageAbout
};

typedef NS_ENUM(NSInteger, PLManagementMode) { PLManagementModeBasic, PLManagementModeAdvanced };

typedef NS_ENUM(NSInteger, PLClientColumn) {
  PLClientColumnStatus,
  PLClientColumnClient,
  PLClientColumnBackend,
  PLClientColumnRole,
  PLClientColumnLastSeen
};

/// Immutable navigation values. Visibility and fallback selection come from portable policy.
@interface PLNavigationItem : NSObject
@property(nonatomic, readonly) PLManagementPage page;
@property(nonatomic, readonly, copy) NSString* title;
@property(nonatomic, readonly, copy) NSString* symbol;
@property(nonatomic, readonly, copy) NSString* automationIdentifier;
@property(nonatomic, readonly) BOOL footer;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

/// A copied client presentation value, with no references into gateway state or credentials.
@interface PLClientRow : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* name;
@property(nonatomic, readonly, copy) NSString* backend;
@property(nonatomic, readonly, copy) NSString* role;
@property(nonatomic, readonly, copy) NSString* lastSeen;
@property(nonatomic, readonly, copy) NSString* identificationCode;
@property(nonatomic, readonly, copy) NSString* deviceSymbol;
@property(nonatomic, readonly) BOOL connected;
@property(nonatomic, readonly) BOOL canSetOperator;
@property(nonatomic, readonly) BOOL canSetViewer;
@property(nonatomic, readonly, nullable) NSNumber* attachedScriptCount;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

/// Adapter action availability is supplied by portable policy, not inferred from display strings.
@interface PLAdapterRow : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* name;
@property(nonatomic, readonly, copy) NSString* gatewayState;
@property(nonatomic, readonly, copy) NSString* backendState;
@property(nonatomic, readonly, copy) NSString* detail;
@property(nonatomic, readonly) BOOL canStart;
@property(nonatomic, readonly) BOOL canStop;
@property(nonatomic, readonly) BOOL canRestart;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

@interface PLStatusRow : NSObject
@property(nonatomic, readonly, copy) NSString* name;
@property(nonatomic, readonly, copy) NSString* state;
@property(nonatomic, readonly, copy) NSString* detail;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

/// Connection presentation never contains the stored OBS password.
@interface PLObsConnection : NSObject
@property(nonatomic, readonly, copy) NSString* host;
@property(nonatomic, readonly) NSInteger port;
@property(nonatomic, readonly, copy) NSString* status;
@property(nonatomic, readonly) BOOL connected;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

@interface PLNetworkSettings : NSObject
@property(nonatomic, readonly, copy) NSString* address;
@property(nonatomic, readonly, copy) NSArray<NSString*>* availableAddresses;
@property(nonatomic, readonly) NSInteger port;
@property(nonatomic, readonly) BOOL listening;
@property(nonatomic, readonly, copy) NSString* warning;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

/// macOS companion preferences intentionally omit unsupported vMix installation settings.
@interface PLCompanionPreferences : NSObject
@property(nonatomic, readonly) PLManagementMode interfaceMode;
@property(nonatomic, readonly) BOOL singleOperator;
@property(nonatomic, readonly) BOOL signInStartup;
@property(nonatomic, readonly) BOOL automaticHeadroom;
@property(nonatomic, readonly) NSInteger headroom;
@property(nonatomic, readonly) BOOL auditionBus;
@property(nonatomic, readonly, copy) NSString* obsDirectory;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

NS_ASSUME_NONNULL_END
