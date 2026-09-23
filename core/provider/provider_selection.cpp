#include <studio_remote/provider/provider_selection.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace studio_remote
{

bool ProviderSelection::contains(std::string_view id) const
{
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

ProviderSelection parse_provider_selection(std::string_view value,
                                           std::span<const std::string_view> built_provider_ids)
{
  if (value.empty())
    throw std::invalid_argument("Provider list must not be empty");

  std::unordered_set<std::string> requested;
  std::size_t position = 0;
  while (position <= value.size())
  {
    const auto separator = value.find(',', position);
    const auto end = separator == std::string_view::npos ? value.size() : separator;
    const auto item = value.substr(position, end - position);
    if (item.empty())
      throw std::invalid_argument("Provider list contains an empty entry");
    if (std::find(built_provider_ids.begin(), built_provider_ids.end(), item) ==
        built_provider_ids.end())
      throw std::invalid_argument("Provider is not built for production: " + std::string(item));
    if (!requested.emplace(item).second)
      throw std::invalid_argument("Provider list contains a duplicate entry: " + std::string(item));
    if (separator == std::string_view::npos)
      break;
    position = separator + 1;
  }

  ProviderSelection result;
  for (const auto built_id : built_provider_ids)
    if (built_id == "companion-teleprompter" || requested.contains(std::string(built_id)))
      result.ids.emplace_back(built_id);
  for (const auto& id : result.ids)
  {
    if (!result.normalized.empty())
      result.normalized.push_back(',');
    result.normalized += id;
  }
  return result;
}

} // namespace studio_remote
