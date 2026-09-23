#pragma once

#import "ManagementTeleprompter.h"
#include <studio_remote/ui/teleprompter_controller.hpp>

namespace studio_remote::macos
{
PLTeleprompterState* teleprompter_state(const TeleprompterUiState& state);
}
