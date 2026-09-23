#pragma once

#include <studio_remote/common/types.hpp>

namespace studio_remote::macos
{
/// Bounded local process, memory/power and active-interface observations. No network requests,
/// UI, credentials, addresses, interface names, command lines or media are collected.
json macos_host_diagnostics();
} // namespace studio_remote::macos
