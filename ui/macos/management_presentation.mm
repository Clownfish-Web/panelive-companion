#include "management_presentation.hpp"
#include "management_text.hpp"

#include <array>
#include <stdexcept>

namespace
{
using studio_remote::macos::native_text;

NSString* device_symbol(studio_remote::DeviceType type)
{
  switch (type)
  {
  case studio_remote::DeviceType::smartphone:
    return @"iphone";
  case studio_remote::DeviceType::tablet:
    return @"ipad";
  case studio_remote::DeviceType::unknown:
    return @"desktopcomputer";
  }
  throw std::invalid_argument("Management presentation contains an invalid device type");
}
} // namespace

@interface PLNavigationItem ()
- (instancetype)initWithPage:(PLManagementPage)page
                       title:(NSString*)title
                      symbol:(NSString*)symbol
        automationIdentifier:(NSString*)automationIdentifier
                      footer:(BOOL)footer;
@end

@implementation PLNavigationItem
- (instancetype)initWithPage:(PLManagementPage)page
                       title:(NSString*)title
                      symbol:(NSString*)symbol
        automationIdentifier:(NSString*)automationIdentifier
                      footer:(BOOL)footer
{
  self = [super init];
  if (self)
  {
    _page = page;
    _title = [title copy];
    _symbol = [symbol copy];
    _automationIdentifier = [automationIdentifier copy];
    _footer = footer;
  }
  return self;
}
@end

@interface PLClientRow ()
- (instancetype)initWithRow:(const studio_remote::ManagementClientRow&)row;
@end

@implementation PLClientRow
- (instancetype)initWithRow:(const studio_remote::ManagementClientRow&)row
{
  self = [super init];
  if (self)
  {
    _identifier = native_text(row.id);
    _name = native_text(row.name);
    _backend = native_text(row.backend);
    _role = native_text(row.role);
    _lastSeen = native_text(row.last_seen);
    _identificationCode = native_text(row.identification_code);
    _deviceSymbol = device_symbol(row.device_type);
    _connected = row.connected;
    _canSetOperator = row.can_set_operator;
    _canSetViewer = row.can_set_viewer;
    _attachedScriptCount = row.attached_script_count ? @(*row.attached_script_count) : nil;
  }
  return self;
}
@end

@interface PLAdapterRow ()
- (instancetype)initWithRow:(const studio_remote::ManagementAdapterRow&)row;
@end

@implementation PLAdapterRow
- (instancetype)initWithRow:(const studio_remote::ManagementAdapterRow&)row
{
  self = [super init];
  if (self)
  {
    _identifier = native_text(row.provider_id);
    _name = native_text(row.name);
    _gatewayState = native_text(row.gateway_state);
    _backendState = native_text(row.backend_state);
    _detail = native_text(row.detail);
    _canStart = row.can_start;
    _canStop = row.can_stop;
    _canRestart = row.can_restart;
  }
  return self;
}
@end

@interface PLStatusRow ()
- (instancetype)initWithRow:(const studio_remote::ManagementStatusRow&)row;
@end

@implementation PLStatusRow
- (instancetype)initWithRow:(const studio_remote::ManagementStatusRow&)row
{
  self = [super init];
  if (self)
  {
    _name = native_text(row.name);
    _state = native_text(row.state);
    _detail = native_text(row.detail);
  }
  return self;
}
@end

@interface PLObsConnection ()
- (instancetype)initWithConnection:(const studio_remote::ManagementObsConnection&)connection;
@end

@implementation PLObsConnection
- (instancetype)initWithConnection:(const studio_remote::ManagementObsConnection&)connection
{
  self = [super init];
  if (self)
  {
    _host = native_text(connection.host);
    _port = connection.port;
    _status = native_text(connection.status);
    _connected = connection.connected;
  }
  return self;
}
@end

@interface PLNetworkSettings ()
- (instancetype)initWithSettings:(const studio_remote::ManagementNetworkSettings&)settings;
@end

@implementation PLNetworkSettings
- (instancetype)initWithSettings:(const studio_remote::ManagementNetworkSettings&)settings
{
  self = [super init];
  if (self)
  {
    _address = native_text(settings.address);
    auto* addresses =
        [NSMutableArray<NSString*> arrayWithCapacity:settings.available_addresses.size()];
    for (const auto& address : settings.available_addresses)
      [addresses addObject:native_text(address)];
    _availableAddresses = [addresses copy];
    _port = settings.port;
    _listening = settings.listening;
    _warning = native_text(settings.warning);
  }
  return self;
}
@end

@interface PLCompanionPreferences ()
- (instancetype)initWithMode:(studio_remote::ManagementInterfaceMode)mode
                       basic:(const studio_remote::ManagementBasicSettings&)basic
                    advanced:(const studio_remote::ManagementAdvancedSettings&)advanced;
@end

@implementation PLCompanionPreferences
- (instancetype)initWithMode:(studio_remote::ManagementInterfaceMode)mode
                       basic:(const studio_remote::ManagementBasicSettings&)basic
                    advanced:(const studio_remote::ManagementAdvancedSettings&)advanced
{
  self = [super init];
  if (self)
  {
    _interfaceMode = mode == studio_remote::ManagementInterfaceMode::advanced
                         ? PLManagementModeAdvanced
                         : PLManagementModeBasic;
    _singleOperator = basic.single_operator_mode;
    _signInStartup = basic.sign_in_startup;
    _automaticHeadroom = advanced.automatic_headroom;
    _headroom = advanced.headroom_per_additional_source_db;
    _auditionBus = advanced.audio_source_audition_bus;
    _obsDirectory = native_text(advanced.obs_directory);
  }
  return self;
}
@end

namespace studio_remote::macos
{
namespace
{
struct NavigationDefinition
{
  ManagementPage page;
  NSString* title;
  NSString* symbol;
  NSString* automation_identifier;
  bool footer;
};
} // namespace

PLManagementPage native_page(ManagementPage page)
{
  switch (page)
  {
  case ManagementPage::clients:
    return PLManagementPageClients;
  case ManagementPage::teleprompter:
    return PLManagementPageTeleprompter;
  case ManagementPage::adapters:
    return PLManagementPageAdapters;
  case ManagementPage::companion_settings:
    return PLManagementPageCompanionSettings;
  case ManagementPage::status:
    return PLManagementPageStatus;
  case ManagementPage::diagnostics:
    return PLManagementPageDiagnostics;
  case ManagementPage::advanced_settings:
    return PLManagementPageAdvancedSettings;
  case ManagementPage::about:
    return PLManagementPageAbout;
  }
  throw std::invalid_argument("Management page is invalid");
}

ManagementPage portable_page(PLManagementPage page)
{
  switch (page)
  {
  case PLManagementPageClients:
    return ManagementPage::clients;
  case PLManagementPageTeleprompter:
    return ManagementPage::teleprompter;
  case PLManagementPageAdapters:
    return ManagementPage::adapters;
  case PLManagementPageCompanionSettings:
    return ManagementPage::companion_settings;
  case PLManagementPageStatus:
    return ManagementPage::status;
  case PLManagementPageDiagnostics:
    return ManagementPage::diagnostics;
  case PLManagementPageAdvancedSettings:
    return ManagementPage::advanced_settings;
  case PLManagementPageAbout:
    return ManagementPage::about;
  }
  throw std::invalid_argument("Management page is invalid");
}

ManagementInterfaceMode portable_mode(PLManagementMode mode)
{
  switch (mode)
  {
  case PLManagementModeBasic:
    return ManagementInterfaceMode::basic;
  case PLManagementModeAdvanced:
    return ManagementInterfaceMode::advanced;
  }
  throw std::invalid_argument("Management interface mode is invalid");
}

NSArray<PLNavigationItem*>* navigation_items(ManagementInterfaceMode mode)
{
  const std::array definitions{
      NavigationDefinition{
          ManagementPage::clients, @"Clients", @"ipad.and.iphone", @"route-clients", false},
      NavigationDefinition{ManagementPage::adapters,
                           @"Adapters",
                           @"arrow.left.arrow.right",
                           @"route-adapters",
                           false},
      NavigationDefinition{ManagementPage::teleprompter,
                           @"Teleprompter",
                           @"text.book.closed",
                           @"route-teleprompter",
                           false},
      NavigationDefinition{ManagementPage::companion_settings,
                           @"Companion Settings",
                           @"network",
                           @"route-companion-settings",
                           false},
      NavigationDefinition{
          ManagementPage::status, @"Status", @"checkmark.circle", @"route-status", false},
      NavigationDefinition{ManagementPage::diagnostics,
                           @"Diagnostics",
                           @"waveform.path.ecg",
                           @"route-diagnostics",
                           false},
      NavigationDefinition{ManagementPage::advanced_settings,
                           @"Advanced Settings",
                           @"gearshape",
                           @"route-advanced-settings",
                           true},
      NavigationDefinition{ManagementPage::about, @"About", @"info.circle", @"route-about", true}};
  auto* result = [NSMutableArray<PLNavigationItem*> arrayWithCapacity:definitions.size()];
  for (const auto& definition : definitions)
  {
    if (!management_page_visible(mode, definition.page))
      continue;
    [result addObject:[[PLNavigationItem alloc] initWithPage:native_page(definition.page)
                                                       title:definition.title
                                                      symbol:definition.symbol
                                        automationIdentifier:definition.automation_identifier
                                                      footer:definition.footer]];
  }
  return [result copy];
}

NSArray<PLClientRow*>* client_rows(const std::vector<ManagementClientRow>& clients)
{
  auto* result = [NSMutableArray<PLClientRow*> arrayWithCapacity:clients.size()];
  for (const auto& client : clients)
    [result addObject:[[PLClientRow alloc] initWithRow:client]];
  return [result copy];
}
NSArray<PLAdapterRow*>* adapter_rows(const std::vector<ManagementAdapterRow>& adapters)
{
  auto* result = [NSMutableArray<PLAdapterRow*> arrayWithCapacity:adapters.size()];
  for (const auto& adapter : adapters)
    [result addObject:[[PLAdapterRow alloc] initWithRow:adapter]];
  return [result copy];
}

NSArray<PLStatusRow*>* status_rows(const std::vector<ManagementStatusRow>& feeds)
{
  auto* result = [NSMutableArray<PLStatusRow*> arrayWithCapacity:feeds.size()];
  for (const auto& feed : feeds)
    [result addObject:[[PLStatusRow alloc] initWithRow:feed]];
  return [result copy];
}
PLObsConnection* obs_connection(const ManagementObsConnection& connection)
{
  return [[PLObsConnection alloc] initWithConnection:connection];
}
PLNetworkSettings* network_settings(const ManagementNetworkSettings& settings)
{
  return [[PLNetworkSettings alloc] initWithSettings:settings];
}
PLCompanionPreferences* companion_preferences(ManagementInterfaceMode mode,
                                              const ManagementBasicSettings& basic,
                                              const ManagementAdvancedSettings& advanced)
{
  return [[PLCompanionPreferences alloc] initWithMode:mode basic:basic advanced:advanced];
}
} // namespace studio_remote::macos
