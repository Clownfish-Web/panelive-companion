#pragma once

namespace studio_remote::macos
{
/// Main-thread startup UI, shown only after noninteractive Keychain access is denied.
bool request_keychain_startup_authorization();
} // namespace studio_remote::macos
