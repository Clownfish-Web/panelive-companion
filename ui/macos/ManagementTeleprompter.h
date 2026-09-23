#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, PLTeleprompterRole) {
  PLTeleprompterRoleMaster = 0,
  PLTeleprompterRoleController = 1,
  PLTeleprompterRoleMonitor = 2
};

FOUNDATION_EXPORT const NSInteger PLTeleprompterMaximumContentLength;

/// Immutable copies of shared controller state; no native view owns storage or policy.
@interface PLTeleprompterScript : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly, copy) NSString* name;
@property(nonatomic, readonly) uint64_t revision;
@property(nonatomic, readonly) uint64_t createdAtUnixMs;
@property(nonatomic, readonly) uint64_t modifiedAtUnixMs;
@property(nonatomic, readonly) uint64_t wordCount;
@property(nonatomic, readonly) uint64_t estimatedDurationMs;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

@interface PLTeleprompterSession : NSObject
@property(nonatomic, readonly, copy) NSString* identifier;
@property(nonatomic, readonly) uint64_t version;
@property(nonatomic, readonly) NSUInteger scriptCount;
@property(nonatomic, readonly) NSUInteger memberCount;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

@interface PLTeleprompterState : NSObject
@property(nonatomic, readonly) BOOL libraryView;
@property(nonatomic, readonly, copy) NSArray<PLTeleprompterScript*>* library;
@property(nonatomic, readonly, copy) NSString* deviceIdentifier;
@property(nonatomic, readonly, copy) NSString* clientName;
@property(nonatomic, readonly, copy) NSString* error;
@property(nonatomic, readonly) uint64_t formGeneration;
@property(nonatomic, readonly) BOOL editing;
@property(nonatomic, readonly) BOOL editingExisting;
@property(nonatomic, readonly) BOOL sharing;
@property(nonatomic, readonly) BOOL hasUnsavedChanges;
@property(nonatomic, readonly, copy) NSString* name;
@property(nonatomic, readonly, copy) NSString* content;
@property(nonatomic, readonly) NSInteger readingSpeed;
@property(nonatomic, readonly, copy) NSArray<PLTeleprompterScript*>* scripts;
@property(nonatomic, readonly, copy) NSArray<PLTeleprompterSession*>* sessions;
@property(nonatomic, readonly, copy) NSArray<NSString*>* selectedScripts;
@property(nonatomic, readonly, copy) NSArray<NSString*>* selectedClients;
@property(nonatomic, readonly, copy) NSDictionary<NSString*, NSNumber*>* clientRoles;
@property(nonatomic, readonly) PLTeleprompterRole ownerRole;
@property(nonatomic, readonly, copy) NSDictionary<NSString*, NSArray<NSNumber*>*>* roleChoices;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

NS_ASSUME_NONNULL_END
