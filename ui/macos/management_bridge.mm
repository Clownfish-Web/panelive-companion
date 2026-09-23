#include "management_bridge.hpp"

#include "management_diagnostics.hpp"
#include "management_pairing.hpp"
#include "management_presentation.hpp"
#include "management_teleprompter.hpp"
#include "management_text.hpp"
#include "management_worker.hpp"

#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

using studio_remote::ManagementInterfaceMode;
using studio_remote::ManagementUiController;

namespace
{
struct PendingPresentation
{
  std::mutex mutex;
  std::optional<studio_remote::macos::ManagementUpdate> update;
  bool scheduled = false;
  bool valid = true;

  bool publish(studio_remote::macos::ManagementUpdate value)
  {
    std::lock_guard lock(mutex);
    if (!valid)
      return false;
    value.command_result = value.command_result || (update && update->command_result);
    update = std::move(value);
    return !std::exchange(scheduled, true);
  }
  std::optional<studio_remote::macos::ManagementUpdate> take()
  {
    std::lock_guard lock(mutex);
    scheduled = false;
    return std::exchange(update, std::nullopt);
  }
  void invalidate()
  {
    std::lock_guard lock(mutex);
    valid = false;
    update.reset();
  }
};

std::string request_text(NSString* value, const char* invalid_message)
{
  NSData* bytes = [value dataUsingEncoding:NSUTF8StringEncoding];
  if (!bytes)
    throw std::invalid_argument(invalid_message);
  if (bytes.length == 0)
    return {};
  return std::string(static_cast<const char*>(bytes.bytes), bytes.length);
}

std::string identifier_text(NSString* value, const char* missing_message)
{
  NSData* bytes = [value dataUsingEncoding:NSUTF8StringEncoding];
  if (!bytes || bytes.length == 0)
    throw std::invalid_argument(missing_message);
  return std::string(static_cast<const char*>(bytes.bytes), bytes.length);
}
} // namespace

@interface PLManagementBridge ()
- (instancetype)initWithController:(std::unique_ptr<ManagementUiController>)controller;
- (instancetype)initWithWorker:(studio_remote::macos::ManagementWorker&)worker;
- (void)performCommand:(const studio_remote::macos::ManagementWorker::Command&)command;
- (void)performCommand:(const studio_remote::macos::ManagementWorker::Command&)command
             isRefresh:(BOOL)refresh;
- (void)updatePresentation;
- (void)publishState:(const studio_remote::ManagementUiState&)state;
@end

@implementation PLManagementBridge
{
  std::unique_ptr<ManagementUiController> _controller;
  studio_remote::macos::ManagementWorker* _worker;
  std::shared_ptr<PendingPresentation> _pendingPresentation;
}

- (instancetype)initWithController:(std::unique_ptr<ManagementUiController>)controller
{
  self = [super init];
  if (self)
  {
    _controller = std::move(controller);
    _teleprompter = studio_remote::macos::teleprompter_state({});
    _navigation = @[];
    _clients = @[];
    _adapters = @[];
    _statusFeeds = @[];
    _obsConnection = studio_remote::macos::obs_connection({});
    _streamlabsConnection = studio_remote::macos::obs_connection({"127.0.0.1", 59650, "", false});
    _networkSettings = studio_remote::macos::network_settings({});
    _diagnostics = studio_remote::macos::diagnostics({}, {}, {});
    _preferences =
        studio_remote::macos::companion_preferences(ManagementInterfaceMode::basic, {}, {});
    _lastError = @"";
    _identificationTitle = @"";
    _interfaceMode = PLManagementModeBasic;
    _initialPage = PLManagementPageClients;
    [self refresh];
  }
  return self;
}

- (BOOL)isValid
{
  return _controller != nullptr || _worker != nullptr;
}

- (instancetype)initWithWorker:(studio_remote::macos::ManagementWorker&)worker
{
  self = [self initWithController:nullptr];
  if (self)
  {
    _worker = &worker;
    _pendingPresentation = std::make_shared<PendingPresentation>();
    [self publishState:worker.snapshot()];
    __weak PLManagementBridge* target = self;
    std::weak_ptr<PendingPresentation> pending = _pendingPresentation;
    worker.on_snapshot(
        [target, pending](studio_remote::macos::ManagementUpdate update)
        {
          auto delivery = pending.lock();
          if (!delivery || !delivery->publish(std::move(update)))
            return;
          dispatch_async(dispatch_get_main_queue(), ^{
            PLManagementBridge* bridge = target;
            auto value = delivery->take();
            if (!bridge.valid || !value)
              return;
            try
            {
              auto* teleprompter = studio_remote::macos::teleprompter_state(value->teleprompter);
              [bridge publishState:value->state];
              bridge->_teleprompter = teleprompter;
              if (bridge.presentationChanged)
                bridge.presentationChanged(value->command_result);
            }
            catch (...)
            {
              bridge->_lastError = @"Management presentation failed";
              NSLog(@"Management snapshot conversion failed");
            }
          });
        });
    [self refresh];
  }
  return self;
}

- (void)updatePresentation
{
  _controller->expire_pairing();
  auto* teleprompter =
      studio_remote::macos::teleprompter_state(_controller->teleprompter().state());
  [self publishState:_controller->state()];
  _teleprompter = teleprompter;
}

- (void)publishState:(const studio_remote::ManagementUiState&)state
{
  // Never retain an earlier QR when the current pairing or another snapshot cannot be rendered.
  _pairing = nil;
  // Complete conversion before publication so a failure cannot publish half a state.
  auto* navigation = studio_remote::macos::navigation_items(state.interface_mode);
  auto* clients = studio_remote::macos::client_rows(state.clients);
  auto* pairing = state.pairing ? studio_remote::macos::pairing_code(*state.pairing) : nil;
  auto* adapters = studio_remote::macos::adapter_rows(state.adapters);
  auto* feeds = studio_remote::macos::status_rows(state.status_feeds);
  auto* obs = studio_remote::macos::obs_connection(state.obs_connection);
  auto* streamlabs = studio_remote::macos::obs_connection(state.streamlabs_connection);
  auto* network = studio_remote::macos::network_settings(state.network_settings);
  auto* diagnostics = studio_remote::macos::diagnostics(
      state.endurance_status, state.audio_outputs, state.audio_status);
  auto* preferences = studio_remote::macos::companion_preferences(
      state.interface_mode, state.basic_settings, state.advanced_settings);
  const auto initialPage = studio_remote::macos::native_page(state.initial_page);
  NSString* error = [[NSString alloc] initWithBytes:state.last_error.data()
                                             length:state.last_error.size()
                                           encoding:NSUTF8StringEncoding];
  _navigation = navigation;
  _clients = clients;
  _clientSortColumn = static_cast<PLClientColumn>(state.client_sort.column);
  _clientSortAscending =
      state.client_sort.direction == studio_remote::ManagementSortDirection::ascending;
  _pairing = pairing;
  _adapters = adapters;
  _statusFeeds = feeds;
  _obsConnection = obs;
  _streamlabsConnection = streamlabs;
  _networkSettings = network;
  _diagnostics = diagnostics;
  _preferences = preferences;
  _lastError = error ? error : @"Management operation failed";
  _interfaceMode = state.interface_mode == ManagementInterfaceMode::advanced
                       ? PLManagementModeAdvanced
                       : PLManagementModeBasic;
  _initialPage = initialPage;
  _identificationPending = state.identification.has_value();
  _identificationTitle =
      state.identification ? studio_remote::macos::native_text(state.identification->title) : @"";
}

- (void)performCommand:(const studio_remote::macos::ManagementWorker::Command&)command
{
  [self performCommand:command isRefresh:NO];
}

- (void)performCommand:(const studio_remote::macos::ManagementWorker::Command&)command
             isRefresh:(BOOL)refresh
{
  if (![NSThread isMainThread])
  {
    // Objective-C callers must follow the same isolation enforced for Swift by MainActor.
    NSLog(@"Rejected a management command outside the main thread");
    return;
  }
  if (!self.valid)
    return;
  try
  {
    if (_worker)
    {
      if (refresh)
        _worker->refresh();
      else
        _worker->post(command);
      return;
    }
    command(*_controller);
    [self updatePresentation];
  }
  catch (const std::exception& error)
  {
    if (_controller)
      _controller->report_error(error.what());
    NSString* message = [NSString stringWithUTF8String:error.what()];
    _lastError = message ? message : @"Management operation failed";
  }
  catch (...)
  {
    if (_controller)
      _controller->report_error("Management operation failed");
    _lastError = @"Management operation failed";
  }
}

- (void)refresh
{
  [self performCommand:[](ManagementUiController& controller) { controller.refresh(); }
             isRefresh:YES];
}

- (void)setInterfaceMode:(PLManagementMode)mode
{
  [self performCommand:[mode](ManagementUiController& controller)
        { controller.set_interface_mode(studio_remote::macos::portable_mode(mode)); }];
}

- (PLManagementPage)visiblePageForPage:(PLManagementPage)page
{
  if (![NSThread isMainThread] || !self.valid)
    return PLManagementPageClients;
  return studio_remote::macos::native_page(
      studio_remote::visible_management_page(studio_remote::macos::portable_mode(_interfaceMode),
                                             studio_remote::macos::portable_page(page)));
}

- (void)pairClient
{
  [self performCommand:[](ManagementUiController& controller) { controller.pair_client(); }];
}

- (void)requestPairing:(void (^)(NSString*, uint64_t))completion
{
  __weak PLManagementBridge* target = self;
  [self performCommand:[target, completion](ManagementUiController& controller)
        {
          controller.pair_client();
          if (!controller.state().pairing)
            return;
          const auto& pairing = *controller.state().pairing;
          NSString* payload = studio_remote::macos::native_text(pairing.payload);
          const auto expires = pairing.expires_unix_seconds;
          auto deliver = ^{
            if (!target.valid)
              return;
            try
            {
              completion(payload, expires);
            }
            catch (...)
            {
              NSLog(@"Could not present tray pairing");
            }
          };
          dispatch_async(dispatch_get_main_queue(), deliver);
        }];
}

- (void)clearPairing
{
  [self performCommand:[](ManagementUiController& controller) { controller.clear_pairing(); }];
}

- (void)showPairingPayload:(NSString*)payload expiresUnixSeconds:(uint64_t)expires
{
  [self performCommand:[payload, expires](ManagementUiController& controller)
        {
          controller.show_pairing(
              {request_text(payload, "Pairing payload contains invalid text"), expires});
        }];
}

- (void)sortClients:(PLClientColumn)column
{
  [self performCommand:[column](ManagementUiController& controller)
        {
          if (column < PLClientColumnStatus || column > PLClientColumnLastSeen)
            throw std::invalid_argument("Invalid client sort column");
          controller.sort_clients(static_cast<studio_remote::ManagementClientColumn>(column));
        }];
}

- (void)refreshClients
{
  [self performCommand:[](ManagementUiController& controller) { controller.refresh_clients(); }];
}

- (void)setOperator:(NSString*)deviceIdentifier
{
  [self
      performCommand:[deviceIdentifier](ManagementUiController& controller)
      {
        controller.set_operator(identifier_text(deviceIdentifier, "Client identifier is required"));
      }];
}

- (void)setViewer:(NSString*)deviceIdentifier
{
  [self performCommand:[deviceIdentifier](ManagementUiController& controller)
        {
          controller.set_viewer(identifier_text(deviceIdentifier, "Client identifier is required"));
        }];
}

- (void)identifyClient:(NSString*)deviceIdentifier
{
  [self performCommand:[deviceIdentifier](ManagementUiController& controller)
        {
          controller.identify_client(
              identifier_text(deviceIdentifier, "Client identifier is required"));
        }];
}

- (void)identifyAllClients
{
  [self
      performCommand:[](ManagementUiController& controller) { controller.identify_all_clients(); }];
}

- (void)confirmClientIdentificationAudible:(BOOL)audible vibrate:(BOOL)vibrate
{
  [self performCommand:[audible, vibrate](ManagementUiController& controller)
        { controller.confirm_client_identification({audible != NO, vibrate != NO}); }];
}

- (void)cancelClientIdentification
{
  [self performCommand:[](ManagementUiController& controller)
        { controller.cancel_client_identification(); }];
}

- (void)forgetClient:(NSString*)deviceIdentifier
{
  [self performCommand:[deviceIdentifier](ManagementUiController& controller)
        {
          controller.forget_client(
              identifier_text(deviceIdentifier, "Client identifier is required"));
        }];
}

- (void)startAdapter:(NSString*)providerIdentifier
{
  [self performCommand:[providerIdentifier](ManagementUiController& controller)
        {
          controller.start_adapter(
              identifier_text(providerIdentifier, "Adapter identifier is required"));
        }];
}

- (void)stopAdapter:(NSString*)providerIdentifier
{
  [self performCommand:[providerIdentifier](ManagementUiController& controller)
        {
          controller.stop_adapter(
              identifier_text(providerIdentifier, "Adapter identifier is required"));
        }];
}

- (void)restartAdapter:(NSString*)providerIdentifier
{
  [self performCommand:[providerIdentifier](ManagementUiController& controller)
        {
          controller.restart_adapter(
              identifier_text(providerIdentifier, "Adapter identifier is required"));
        }];
}

- (void)applyObsHost:(NSString*)host port:(NSInteger)port password:(NSString*)password
{
  [self performCommand:[host, port, password](ManagementUiController& controller)
        {
          if (port < 1 || port > 65'535)
            throw std::invalid_argument("OBS port is out of range");
          studio_remote::ManagementObsConnectionRequest request;
          request.host = request_text(host, "OBS connection contains invalid text");
          request.port = static_cast<std::uint16_t>(port);
          if (password.length != 0)
            request.password = request_text(password, "OBS connection contains invalid text");
          controller.apply_obs_connection(std::move(request));
        }];
}

- (void)applyStreamlabsPort:(NSInteger)port token:(NSString*)token
{
  [self performCommand:[port, token](ManagementUiController& controller)
        {
          if (port < 1 || port > 65'535)
            throw std::invalid_argument("Streamlabs port is out of range");
          studio_remote::ManagementObsConnectionRequest request;
          request.host = "127.0.0.1";
          request.port = static_cast<std::uint16_t>(port);
          if (token.length != 0)
            request.password = request_text(token, "Streamlabs token contains invalid text");
          controller.apply_streamlabs_connection(std::move(request));
        }];
}

- (void)applyNetworkAddress:(NSString*)address port:(NSInteger)port
{
  [self performCommand:[address, port](ManagementUiController& controller)
        {
          if (port < 1 || port > 65'535)
            throw std::invalid_argument("Gateway port is out of range");
          studio_remote::ManagementNetworkSettingsRequest request;
          request.address = request_text(address, "Network settings contain invalid text");
          request.port = static_cast<std::uint16_t>(port);
          controller.apply_network_settings(std::move(request));
        }];
}

- (void)setSingleOperator:(BOOL)enabled
{
  [self performCommand:[enabled](ManagementUiController& controller)
        { controller.set_single_operator_mode(enabled); }];
}

- (void)setSignInStartup:(BOOL)enabled
{
  [self performCommand:[enabled](ManagementUiController& controller)
        { controller.set_sign_in_startup(enabled); }];
}

- (void)applyAutomaticHeadroom:(BOOL)automatic
                      headroom:(NSInteger)headroom
                   auditionBus:(BOOL)audition
{
  [self performCommand:[automatic, headroom, audition](ManagementUiController& controller)
        {
          studio_remote::ManagementAudioMixSettingsRequest request;
          request.automatic_headroom = automatic;
          // Invalid native input becomes an invalid portable value, never a wrapped valid value.
          // Basic mode's preservation of hidden headroom settings remains controller-owned.
          request.headroom_per_additional_source_db =
              headroom >= 1 && headroom <= 4 ? static_cast<std::uint32_t>(headroom) : 0;
          request.audio_source_audition_bus = audition;
          controller.apply_audio_mix(request);
        }];
}

- (void)setEnduranceLogging:(BOOL)enabled
{
  [self performCommand:[enabled](ManagementUiController& controller)
        { controller.set_endurance_logging(enabled); }];
}

- (void)clearDebugLogs
{
  [self performCommand:[](ManagementUiController& controller) { controller.clear_debug_logs(); }];
}

- (void)startAudio:(NSString*)deviceIdentifier tap:(PLDiagnosticTap)tap
{
  [self performCommand:[deviceIdentifier, tap](ManagementUiController& controller)
        {
          controller.start_audio(
              identifier_text(deviceIdentifier, "Audio output identifier is required"),
              studio_remote::macos::portable_tap(tap));
        }];
}

- (void)stopAudio
{
  [self performCommand:[](ManagementUiController& controller) { controller.stop_audio(); }];
}

- (void)setAudioGain:(float)gain
{
  [self performCommand:[gain](ManagementUiController& controller)
        {
          if (!std::isfinite(gain))
            throw std::invalid_argument("Audio gain must be finite");
          controller.set_audio_gain(gain);
        }];
}

- (void)beginAudioDebugLog
{
  [self performCommand:[](ManagementUiController& controller)
        { controller.begin_audio_debug_log(); }];
}

- (void)completeAudioDebugLog
{
  [self performCommand:[](ManagementUiController& controller)
        { controller.complete_audio_debug_log(); }];
}

- (NSString*)audioDebugReport
{
  if (_worker)
  {
    _lastError = @"Use asynchronous report delivery for a running gateway";
    return @"";
  }
  __strong NSString* result = @"";
  [self performCommand:[&result](ManagementUiController& controller)
        { result = studio_remote::macos::native_text(controller.audio_debug_report()); }];
  return result;
}

- (void)requestAudioDebugReport:(void (^)(NSString*))completion
{
  if (!_worker)
  {
    completion([self audioDebugReport]);
    return;
  }
  __weak PLManagementBridge* target = self;
  [self performCommand:[target, completion](ManagementUiController& controller)
        {
          NSString* report = studio_remote::macos::native_text(controller.audio_debug_report());
          NSString* error = studio_remote::macos::native_text(controller.state().last_error);
          dispatch_async(dispatch_get_main_queue(), ^{
            PLManagementBridge* bridge = target;
            if (!bridge.valid)
              return;
            bridge->_lastError = error;
            completion(report);
          });
        }];
}

- (void)shutdown
{
  [self performCommand:[](ManagementUiController& controller) { controller.shutdown(); }];
}

- (void)invalidate
{
  if (![NSThread isMainThread])
  {
    NSLog(@"Rejected management invalidation outside the main thread");
    return;
  }
  _identificationPending = NO;
  _identificationTitle = @"";
  _controller.reset();
  if (_pendingPresentation)
    _pendingPresentation->invalidate();
  if (_worker)
    _worker->on_snapshot({});
  _worker = nullptr;
  _pendingPresentation.reset();
  _presentationChanged = nil;
  _pairing = nil;
  _teleprompter = studio_remote::macos::teleprompter_state({});
  _navigation = @[];
  _clients = @[];
  _adapters = @[];
  _statusFeeds = @[];
  _lastError = @"";
}
@end

namespace studio_remote::macos
{
PLManagementBridge* make_async_management_bridge(ManagementWorker& worker)
{
  if (![NSThread isMainThread])
    throw std::logic_error("Management bridge must be constructed on the main thread");
  return [[PLManagementBridge alloc] initWithWorker:worker];
}

PLManagementBridge*
make_management_bridge(ManagementInterfaceMode mode,
                       ManagementPage initial_page,
                       std::function<void(ManagementInterfaceMode)> mode_changed,
                       ManagementUiCallbacks callbacks)
{
  if (![NSThread isMainThread])
    throw std::logic_error("Management bridge must be constructed on the main thread");
  return [[PLManagementBridge alloc]
      initWithController:std::make_unique<ManagementUiController>(
                             mode, initial_page, std::move(mode_changed), std::move(callbacks))];
}
} // namespace studio_remote::macos
