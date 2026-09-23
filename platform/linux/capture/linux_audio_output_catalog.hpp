#pragma once

#include <string>
#include <vector>

namespace studio_remote::linux
{

struct AudioOutputDevice
{
  std::string id;
  std::string name;
  bool is_default = false;
};

class LinuxAudioOutputCatalog
{
public:
  std::vector<AudioOutputDevice> output_devices() const;
};

} // namespace studio_remote::linux
