#pragma once

#include <studio_remote/teleprompter/runtime_resources.hpp>

namespace studio_remote
{
class AuditStore;
}

namespace studio_remote::teleprompter
{
/// Borrows the authoritative pairing cache; audit must outlive every copy of the returned source.
/// Each call returns an independent current-device snapshot without persistent storage access.
SessionDeviceSource session_device_source(AuditStore& audit);
} // namespace studio_remote::teleprompter
