#pragma once

#include <chrono>

namespace studio_remote
{
// Upper bound for retained client work. Network and production command deadlines stay shorter.
inline constexpr auto maximum_client_request_age = std::chrono::hours(1);
} // namespace studio_remote
