#include <studio_remote/obs/obs_saved_quick_transition_catalog.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <sstream>
#include <studio_remote/obs/obs_quick_transition_catalog.hpp>

namespace studio_remote
{
namespace
{
std::optional<std::string> read_saved_file(const std::filesystem::path& path, std::size_t limit)
{
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::nullopt;
  std::string contents;
  std::array<char, 4096> buffer{};
  while (input)
  {
    const auto remaining = limit - contents.size();
    const auto requested = std::min(buffer.size(), remaining + 1);
    input.read(buffer.data(), static_cast<std::streamsize>(requested));
    const auto count = static_cast<std::size_t>(input.gcount());
    if (input.bad() || count > remaining)
      return std::nullopt;
    contents.append(buffer.data(), count);
    if (input.eof())
      return contents;
  }
  return std::nullopt;
}

std::optional<std::string> scene_collection_file(const std::filesystem::path& configuration_root)
{
  const auto contents = read_saved_file(configuration_root / "global.ini", 1024 * 1024);
  if (!contents)
    return std::nullopt;
  std::istringstream input(*contents);
  std::string line;
  bool basic_section = false;
  while (std::getline(input, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty() && line.front() == '[' && line.back() == ']')
    {
      basic_section = line == "[Basic]";
      continue;
    }
    constexpr std::string_view key = "SceneCollectionFile=";
    if (basic_section && line.starts_with(key))
      return line.substr(key.size());
  }
  return std::nullopt;
}

bool safe_collection_file_name(std::string_view value)
{
  return !value.empty() && value.size() <= 240 && value != "." && value != ".." &&
         value.find('/') == std::string_view::npos && value.find('\\') == std::string_view::npos &&
         value.find(':') == std::string_view::npos && value.find('\0') == std::string_view::npos;
}

std::filesystem::path utf8_path(std::string_view value)
{
  std::u8string converted;
  converted.reserve(value.size());
  for (const unsigned char character : value)
    converted.push_back(static_cast<char8_t>(character));
  return std::filesystem::path(converted);
}

} // namespace

ObsSavedQuickTransitionCatalog
read_obs_saved_quick_transition_catalog(const std::filesystem::path& configuration_root,
                                        const json& obs_transition_catalog)
{
  if (!configuration_root.is_absolute())
    return {};
  try
  {
    const auto collection = scene_collection_file(configuration_root);
    if (!collection || !safe_collection_file_name(*collection))
      return {};
    const auto collection_file = utf8_path(*collection + ".json");
    const auto path = configuration_root / "basic" / "scenes" / collection_file;
    const auto contents = read_saved_file(path, 16 * 1024 * 1024);
    if (!contents)
      return {};
    const auto collection_json = json::parse(*contents, nullptr, false);
    if (collection_json.is_discarded() || !collection_json.is_object())
      return {};
    return {ObsSavedCatalogStatus::available,
            map_obs_quick_transition_catalog(collection_json, obs_transition_catalog)};
  }
  catch (const json::exception&)
  {
    return {ObsSavedCatalogStatus::invalid_fields, json::array()};
  }
  catch (const std::filesystem::filesystem_error&)
  {
    return {};
  }
}
} // namespace studio_remote
