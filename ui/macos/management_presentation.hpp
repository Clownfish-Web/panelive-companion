#pragma once

#import "ManagementPresentation.h"

#include <studio_remote/ui/management_interface.hpp>
#include <studio_remote/ui/management_view.hpp>

namespace studio_remote::macos
{
PLManagementPage native_page(ManagementPage page);
ManagementPage portable_page(PLManagementPage page);
ManagementInterfaceMode portable_mode(PLManagementMode mode);
NSArray<PLNavigationItem*>* navigation_items(ManagementInterfaceMode mode);
NSArray<PLClientRow*>* client_rows(const std::vector<ManagementClientRow>& clients);
NSArray<PLAdapterRow*>* adapter_rows(const std::vector<ManagementAdapterRow>& adapters);
NSArray<PLStatusRow*>* status_rows(const std::vector<ManagementStatusRow>& feeds);
PLObsConnection* obs_connection(const ManagementObsConnection& connection);
PLNetworkSettings* network_settings(const ManagementNetworkSettings& settings);
PLCompanionPreferences* companion_preferences(ManagementInterfaceMode mode,
                                              const ManagementBasicSettings& basic,
                                              const ManagementAdvancedSettings& advanced);
} // namespace studio_remote::macos
