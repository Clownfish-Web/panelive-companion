#include <studio_remote/vmix/vmix_output_geometry.hpp>

#include <charconv>
#include <limits>
#include <string>

namespace studio_remote::vmix
{
namespace
{

constexpr std::uint32_t maximum_dimension = 16'384;

std::optional<std::string_view> setting_value(std::string_view xml, std::string_view name)
{
  const auto setting = std::string("<setting name=\"") + std::string(name) + "\"";
  const auto setting_start = xml.find(setting);
  if (setting_start == std::string_view::npos)
    return std::nullopt;
  const auto setting_end = xml.find("</setting>", setting_start + setting.size());
  if (setting_end == std::string_view::npos)
    return std::nullopt;
  const auto value_start_tag = xml.find("<value>", setting_start + setting.size());
  if (value_start_tag == std::string_view::npos || value_start_tag >= setting_end)
    return std::nullopt;
  const auto value_start = value_start_tag + std::string_view("<value>").size();
  const auto value_end = xml.find("</value>", value_start);
  if (value_end == std::string_view::npos || value_end > setting_end)
    return std::nullopt;
  return xml.substr(value_start, value_end - value_start);
}

std::optional<std::uint32_t> positive_dimension(std::string_view value)
{
  std::uint32_t result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result == 0 ||
      result > maximum_dimension)
    return std::nullopt;
  return result;
}

OutputAspectRatioMode aspect_ratio_mode(std::string_view value)
{
  if (value == "0")
    return OutputAspectRatioMode::normal;
  if (value == "1")
    return OutputAspectRatioMode::widescreen;
  if (value == "2")
    return OutputAspectRatioMode::anamorphic;
  return OutputAspectRatioMode::unknown;
}

} // namespace

double OutputGeometry::canvas_aspect_ratio() const noexcept
{
  return height == 0 ? 0.0 : static_cast<double>(width) / height;
}

std::optional<OutputGeometry> parse_output_geometry(std::string_view settings_xml)
{
  const auto size = setting_value(settings_xml, "OutputSize");
  if (!size)
    return std::nullopt;
  const auto separator = size->find('x');
  if (separator == std::string_view::npos ||
      size->find('x', separator + 1) != std::string_view::npos)
    return std::nullopt;
  const auto width = positive_dimension(size->substr(0, separator));
  const auto height = positive_dimension(size->substr(separator + 1));
  if (!width || !height)
    return std::nullopt;

  OutputGeometry result{*width, *height, OutputAspectRatioMode::unknown};
  if (const auto mode = setting_value(settings_xml, "AspectRatio"))
    result.aspect_ratio_mode = aspect_ratio_mode(*mode);
  return result;
}

} // namespace studio_remote::vmix
