#pragma once

#include <string>
#include <string_view>

namespace studio_remote
{

enum class SourceVisualCapability
{
  unknown,
  visual,
  audio_only
};

struct LocalMediaSource
{
  std::string path;
};

class ISourceMediaInspector
{
public:
  virtual ~ISourceMediaInspector() = default;

  virtual SourceVisualCapability inspect(const LocalMediaSource& source) = 0;
};

constexpr std::string_view source_visual_capability_name(SourceVisualCapability capability)
{
  switch (capability)
  {
  case SourceVisualCapability::visual:
    return "visual";
  case SourceVisualCapability::audio_only:
    return "audio-only";
  case SourceVisualCapability::unknown:
    return "unknown";
  }
  return "unknown";
}

} // namespace studio_remote
