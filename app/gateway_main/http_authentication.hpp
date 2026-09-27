#pragma once

#include <string_view>
#include <studio_remote/common/types.hpp>

namespace studio_remote
{
/// Stable wire rejection shared by ordinary HTTP and late media authorization checks.
json authentication_failure_response(std::string_view reason);
} // namespace studio_remote
