#pragma once

#include <functional>
#include <studio_remote/common/types.hpp>

namespace studio_remote
{
/// Synchronous protocol response mapping after wire completion. A transport request may borrow
/// this callback only until that call returns; it must never retain the callback in a session.
using ObsResponseMapping = std::function<void(json&)>;
} // namespace studio_remote
