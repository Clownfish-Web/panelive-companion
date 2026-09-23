#pragma once

#include <source_location>
#include <studio_remote/common/types.hpp>

namespace studio_remote
{
class IObsAdapter;

/// OBS protocol-boundary mapping shared with the Windows reference. Reads statistics before
/// streaming status; preserves caller provenance and includes only numeric backend fields.
json obs_performance_snapshot(IObsAdapter& adapter, std::source_location caller);
} // namespace studio_remote
