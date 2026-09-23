#pragma once

#include <string>
#include <vector>

namespace studio_remote::macos
{
struct AudioOutputDevice
{
  std::string id;
  std::string name;
  bool is_default = false;
};

/// Local-only output-device choices. Stable device UIDs never enter the mobile API.
class MacosAudioOutputCatalog final
{
public:
  std::vector<AudioOutputDevice> output_devices() const;
};
} // namespace studio_remote::macos
