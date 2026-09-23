#pragma once

#import "ManagementPairing.h"

#include <studio_remote/ui/management_view.hpp>

namespace studio_remote::macos
{
PLPairingCode* pairing_code(const ManagementPairing& pairing);
} // namespace studio_remote::macos
