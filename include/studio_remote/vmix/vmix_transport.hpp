#pragma once

#include <map>
#include <string>
#include <string_view>

namespace studio_remote::vmix
{

class IVmixTransport
{
public:
  virtual ~IVmixTransport() = default;
  virtual std::string read_state() = 0;
  virtual std::string read_shortcuts() = 0;
  virtual void trigger_shortcut(std::string_view identifier) = 0;
  virtual void call(std::string_view function,
                    const std::map<std::string, std::string>& parameters) = 0;
};

} // namespace studio_remote::vmix
