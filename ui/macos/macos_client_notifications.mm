#include "macos_client_notifications.hpp"

#include "management_bridge.hpp"
#include "management_text.hpp"

#include <studio_remote/ui/management_client_notifications.hpp>

#import <UserNotifications/UserNotifications.h>

#include <stdexcept>
#include <utility>

namespace
{
NSString* const connection_notification_identifier = @"client-connections";
}

@interface PLClientNotifications : NSObject <UNUserNotificationCenterDelegate>
- (instancetype)initWithBridge:(PLManagementBridge*)bridge
                 windowVisible:(std::function<bool()>)visible
                openManagement:(std::function<void()>)open;
- (void)observe:(std::span<const studio_remote::ManagementClientRow>)clients;
- (void)invalidate;
@end

@implementation PLClientNotifications
{
  __weak PLManagementBridge* _bridge;
  UNUserNotificationCenter* _center;
  NSTimer* _timer;
  studio_remote::ManagementClientNotifications _transitions;
  std::function<bool()> _visible;
  std::function<void()> _open;
  UNMutableNotificationContent* _pending;
  BOOL _active;
  BOOL _authorizing;
  BOOL _sending;
}

- (instancetype)initWithBridge:(PLManagementBridge*)bridge
                 windowVisible:(std::function<bool()>)visible
                openManagement:(std::function<void()>)open
{
  self = [super init];
  if (self)
  {
    _bridge = bridge;
    _visible = std::move(visible);
    _open = std::move(open);
    _active = YES;
    _authorizing = YES;
    _center = UNUserNotificationCenter.currentNotificationCenter;
    _center.delegate = self;
    __weak PLClientNotifications* weak = self;
    [_center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                           completionHandler:^(BOOL granted, NSError* error) {
                             if (error)
                               NSLog(@"Client notification authorization failed (%ld)",
                                     static_cast<long>(error.code));
                             dispatch_async(dispatch_get_main_queue(), ^{
                               PLClientNotifications* owner = weak;
                               if (!owner || !owner->_active)
                                 return;
                               owner->_authorizing = NO;
                               if (!granted)
                                 owner->_pending = nil;
                               [owner deliverPending];
                             });
                           }];
    _timer = [NSTimer timerWithTimeInterval:5
                                    repeats:YES
                                      block:^(NSTimer*) {
                                        [weak refreshHidden];
                                      }];
    [NSRunLoop.mainRunLoop addTimer:_timer forMode:NSRunLoopCommonModes];
  }
  return self;
}

- (void)refreshHidden
{
  if (!_active)
    return;
  try
  {
    if (!_visible())
      [_bridge refresh];
  }
  catch (...)
  {
    NSLog(@"Client notification refresh failed");
  }
}

- (void)observe:(std::span<const studio_remote::ManagementClientRow>)clients
{
  if (!_active)
    return;
  try
  {
    const auto notice = _transitions.observe(clients);
    if (!notice)
      return;
    UNMutableNotificationContent* content = [UNMutableNotificationContent new];
    content.title = studio_remote::macos::native_text(notice->title);
    content.body = studio_remote::macos::native_text(notice->body);
    content.threadIdentifier = connection_notification_identifier;
    // One replaceable pending value also bounds changes received during native authorization.
    _pending = content;
    [self deliverPending];
  }
  catch (...)
  {
    NSLog(@"Client notification presentation failed");
  }
}

- (void)deliverPending
{
  if (!_active || _authorizing || _sending || !_pending)
    return;
  _sending = YES;
  __weak PLClientNotifications* weak = self;
  [_center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
    dispatch_async(dispatch_get_main_queue(), ^{
      PLClientNotifications* owner = weak;
      if (!owner || !owner->_active)
        return;
      if (settings.authorizationStatus != UNAuthorizationStatusAuthorized &&
          settings.authorizationStatus != UNAuthorizationStatusProvisional)
      {
        owner->_pending = nil;
        owner->_sending = NO;
        return;
      }
      [owner submitPending];
    });
  }];
}

- (void)submitPending
{
  UNNotificationRequest* request =
      [UNNotificationRequest requestWithIdentifier:connection_notification_identifier
                                           content:_pending
                                           trigger:nil];
  _pending = nil;
  __weak PLClientNotifications* weak = self;
  [_center addNotificationRequest:request
            withCompletionHandler:^(NSError* error) {
              if (error)
                NSLog(@"Client notification delivery failed (%ld)", static_cast<long>(error.code));
              dispatch_async(dispatch_get_main_queue(), ^{
                PLClientNotifications* owner = weak;
                if (!owner || !owner->_active)
                  return;
                owner->_sending = NO;
                [owner deliverPending];
              });
            }];
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completion
{
  static_cast<void>(center);
  static_cast<void>(notification);
  __weak PLClientNotifications* weak = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    PLClientNotifications* owner = weak;
    completion(owner && owner->_active
                   ? UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList
                   : UNNotificationPresentationOptionNone);
  });
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completion
{
  static_cast<void>(center);
  __weak PLClientNotifications* weak = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    PLClientNotifications* owner = weak;
    try
    {
      if (owner && owner->_active &&
          [response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier])
        owner->_open();
    }
    catch (...)
    {
      NSLog(@"Could not open management from a client notification");
    }
    completion();
  });
}

- (void)invalidate
{
  _active = NO;
  [_timer invalidate];
  _timer = nil;
  _pending = nil;
  _bridge = nil;
  _visible = {};
  _open = {};
  if (_center.delegate == self)
    _center.delegate = nil;
  [_center
      removePendingNotificationRequestsWithIdentifiers:@[ connection_notification_identifier ]];
}

- (void)dealloc
{
  [_timer invalidate];
  if (_center.delegate == self)
    _center.delegate = nil;
}
@end

namespace studio_remote::macos
{
class MacosClientNotifications::Impl
{
public:
  Impl(PLManagementBridge* bridge, std::function<bool()> visible, std::function<void()> open)
      : bridge_(bridge)
  {
    if (!NSThread.isMainThread)
      throw std::logic_error("Client notifications must be constructed on the main thread");
    notifications_ = [[PLClientNotifications alloc] initWithBridge:bridge
                                                     windowVisible:std::move(visible)
                                                    openManagement:std::move(open)];
    __weak PLClientNotifications* target = notifications_;
    observe_management_clients(bridge, [target](auto clients) { [target observe:clients]; });
  }
  ~Impl()
  {
    observe_management_clients(bridge_, {});
    [notifications_ invalidate];
  }

private:
  __weak PLManagementBridge* bridge_;
  PLClientNotifications* notifications_;
};

MacosClientNotifications::MacosClientNotifications(PLManagementBridge* bridge,
                                                   std::function<bool()> window_visible,
                                                   std::function<void()> open_management)
    : impl_(std::make_unique<Impl>(bridge, std::move(window_visible), std::move(open_management)))
{
}

MacosClientNotifications::~MacosClientNotifications() = default;
} // namespace studio_remote::macos
