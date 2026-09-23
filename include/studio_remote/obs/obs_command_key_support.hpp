#pragma once

namespace studio_remote
{
/// Explicit backend support keeps native Command distinct from Windows-key chords.
enum class ObsCommandKeySupport
{
  unavailable,
  available
};
} // namespace studio_remote
