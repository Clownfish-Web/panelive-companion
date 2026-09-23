#pragma once

#include <optional>
#include <string>

namespace studio_remote::vmix
{

class IVmixInputCategorySource
{
public:
  virtual ~IVmixInputCategorySource() = default;
  virtual bool available() const = 0;
  virtual std::optional<std::string> read_current_preset() = 0;
};

} // namespace studio_remote::vmix
