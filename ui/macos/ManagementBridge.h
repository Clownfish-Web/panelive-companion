#pragma once

#import "ManagementBranding.h"
#import "ManagementPairingPreview.h"
#import "ManagementPresentation.h"
#import "ManagementTeleprompter.h"

NS_ASSUME_NONNULL_BEGIN

/// Main-actor facade over the portable controller. Snapshots remain valid after refresh.
/// The native UI owner invalidates this facade before gateway teardown to release callbacks.
NS_SWIFT_UI_ACTOR
@interface PLManagementBridge : NSObject
@property(nonatomic, readonly, strong) PLTeleprompterState* teleprompter;
@property(nonatomic, readonly, copy) NSArray<PLNavigationItem*>* navigation;
@property(nonatomic, readonly, copy) NSArray<PLClientRow*>* clients;
@property(nonatomic, readonly) PLClientColumn clientSortColumn;
@property(nonatomic, readonly) BOOL clientSortAscending;
@property(nonatomic, readonly, copy) NSArray<PLAdapterRow*>* adapters;
@property(nonatomic, readonly, copy) NSArray<PLStatusRow*>* statusFeeds;
@property(nonatomic, readonly, strong) PLObsConnection* obsConnection;
@property(nonatomic, readonly, strong) PLObsConnection* streamlabsConnection;
@property(nonatomic, readonly, strong) PLNetworkSettings* networkSettings;
@property(nonatomic, readonly, strong) PLCompanionPreferences* preferences;
@property(nonatomic, readonly, strong) PLDiagnostics* diagnostics;
@property(nonatomic, readonly, strong, nullable) PLPairingCode* pairing;
@property(nonatomic, readonly, copy) NSString* lastError;
@property(nonatomic, readonly, copy) NSString* identificationTitle;
@property(nonatomic, readonly) BOOL identificationPending;
@property(nonatomic, readonly) PLManagementMode interfaceMode;
@property(nonatomic, readonly) PLManagementPage initialPage;
@property(nonatomic, readonly, getter=isValid) BOOL valid;
/// Main-thread notification after an asynchronous snapshot. The marker distinguishes command
/// completion from observation; retained native pages preserve drafts for either delivery.
@property(nonatomic, copy, nullable) void (^presentationChanged)(BOOL commandResult);

- (void)refresh;
- (void)setInterfaceMode:(PLManagementMode)mode;
- (PLManagementPage)visiblePageForPage:(PLManagementPage)page;
- (void)refreshClients;
- (void)sortClients:(PLClientColumn)column;
- (void)pairClient;
/// Tray pairing opens the window only when the resulting controller state contains pairing.
/// Completion runs on the main thread and is discarded after invalidation.
- (void)requestPairing:(void (^)(NSString* payload, uint64_t expiresUnixSeconds))completion;
- (void)clearPairing;
/// Used by the application when pairing originates outside this window.
- (void)showPairingPayload:(NSString*)payload expiresUnixSeconds:(uint64_t)expires;
- (void)setOperator:(NSString*)deviceIdentifier;
- (void)setViewer:(NSString*)deviceIdentifier;
/// These commands stage identification for confirmation without sending an event.
- (void)identifyClient:(NSString*)deviceIdentifier;
- (void)identifyAllClients;
- (void)confirmClientIdentificationAudible:(BOOL)audible vibrate:(BOOL)vibrate;
- (void)cancelClientIdentification;

- (void)forgetClient:(NSString*)deviceIdentifier;
- (void)startAdapter:(NSString*)providerIdentifier;
- (void)stopAdapter:(NSString*)providerIdentifier;
- (void)restartAdapter:(NSString*)providerIdentifier;
/// A blank password preserves the stored credential. Passwords are never published in snapshots.
- (void)applyObsHost:(NSString*)host port:(NSInteger)port password:(NSString*)password;
/// A blank token preserves its Keychain value. The host is always loopback.
- (void)applyStreamlabsPort:(NSInteger)port token:(NSString*)token;
- (void)applyNetworkAddress:(NSString*)address port:(NSInteger)port;
/// Enabling single-operator mode requires confirmation in the native presentation.
- (void)setSingleOperator:(BOOL)enabled;
- (void)setSignInStartup:(BOOL)enabled;
/// Enabling the audition bus requires confirmation in the native presentation.
- (void)applyAutomaticHeadroom:(BOOL)automatic
                      headroom:(NSInteger)headroom
                   auditionBus:(BOOL)audition;
/// The presentation must confirm shutdown before invoking this command.
- (void)shutdown;
/// Debug recording is transient and default-off on every launch.
- (void)setEnduranceLogging:(BOOL)enabled;
- (void)clearDebugLogs;
- (void)startAudio:(NSString*)deviceIdentifier tap:(PLDiagnosticTap)tap;
- (void)stopAudio;
- (void)setAudioGain:(float)gain;
- (void)beginAudioDebugLog;
- (void)completeAudioDebugLog;
- (NSString*)audioDebugReport;
- (void)requestAudioDebugReport:(void (^)(NSString* report))completion;
- (void)invalidate;
- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;
@end

NS_SWIFT_UI_ACTOR
@interface PLManagementBridge (Teleprompter)
/// Reads after queued editor changes; completion is discarded after invalidation.
- (void)checkTeleprompterUnsavedChanges:(void (^)(BOOL dirty))completion;
- (void)openTeleprompter:(NSString*)deviceIdentifier name:(NSString*)name;
- (void)closeTeleprompter;
- (void)openTeleprompterLibrary;
- (void)attachTeleprompterScript:(NSString*)identifier;
- (void)detachTeleprompterScript:(NSString*)identifier;
- (void)addTeleprompterScript;
- (void)editTeleprompterScript:(NSString*)identifier;
- (void)setTeleprompterName:(NSString*)name;
- (void)setTeleprompterContent:(NSString*)content;
- (void)setTeleprompterReadingSpeed:(NSInteger)speed;
- (void)saveTeleprompterScript;
- (void)cancelTeleprompterEdit;
- (void)deleteTeleprompterScript:(NSString*)identifier revision:(uint64_t)revision;
- (void)configureTeleprompterSharing;
- (void)selectTeleprompterScript:(NSString*)identifier selected:(BOOL)selected;
- (void)selectTeleprompterClient:(NSString*)identifier selected:(BOOL)selected;
- (void)setTeleprompterRole:(NSString*)identifier role:(PLTeleprompterRole)role;
- (void)saveTeleprompterSharing;
- (void)cancelTeleprompterSharing;
- (void)stopTeleprompterSharing:(NSString*)identifier version:(uint64_t)version;
@end

NS_ASSUME_NONNULL_END
