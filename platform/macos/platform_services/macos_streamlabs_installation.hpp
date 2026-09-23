#pragma once

#include <string>

namespace studio_remote::macos
{
/// Validates the installed native bundle, its signer and same-user running instance.
/// Returns the admitted native release version; failures contain no application data.
std::string verified_streamlabs_version();
} // namespace studio_remote::macos
