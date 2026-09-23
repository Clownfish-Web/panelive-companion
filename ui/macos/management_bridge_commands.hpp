#pragma once

#import "ManagementBridge.h"
#include "management_worker.hpp"

/// Internal named-command delivery, shared by native facade implementation files.
@interface PLManagementBridge (CommandDelivery)
- (void)performCommand:(const studio_remote::macos::ManagementWorker::Command&)command;
@end
