#include "configuration_stream.hpp"

#include <limits>
#include <ostream>
#include <stdexcept>

namespace studio_remote
{
void write_configuration_stream(std::ostream& stream, std::string_view contents)
{
  if (!stream)
    throw std::runtime_error("Could not open configuration temporary file");
  if (contents.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
    throw std::length_error("Configuration exceeds stream size limit");
  try
  {
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  }
  catch (const std::ios_base::failure&)
  {
    throw std::runtime_error("Could not write configuration temporary file");
  }
  if (!stream)
    throw std::runtime_error("Could not write configuration temporary file");
  try
  {
    stream.flush();
  }
  catch (const std::ios_base::failure&)
  {
    throw std::runtime_error("Could not flush configuration temporary file");
  }
  if (!stream)
    throw std::runtime_error("Could not flush configuration temporary file");
}
} // namespace studio_remote
