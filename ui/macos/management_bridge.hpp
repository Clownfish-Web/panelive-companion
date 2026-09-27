#pragma once

#import "ManagementBridge.h"

#include <studio_remote/ui/management_controller.hpp>

#include <span>

namespace studio_remote::macos
{
class ManagementWorker;
/// Production delivery. The root owns the worker until after bridge invalidation, then joins it
/// before releasing backend services. Construction and snapshot presentation are main-thread only.
PLManagementBridge* make_async_management_bridge(ManagementWorker& worker);

using ManagementClientsObserver = std::function<void(std::span<const ManagementClientRow>)>;
/// Main-thread observation of successful snapshots; no initial replay. Invalidation releases it.
void observe_management_clients(PLManagementBridge* bridge, ManagementClientsObserver observer);

/// Construct on the main thread. ARC owns the facade; it uniquely owns the portable controller.
/// Callbacks must read cached values or post bounded commands, never block the native UI.
PLManagementBridge*
make_management_bridge(ManagementInterfaceMode mode,
                       ManagementPage initial_page,
                       std::function<void(ManagementInterfaceMode)> mode_changed,
                       ManagementUiCallbacks callbacks);
} // namespace studio_remote::macos
