#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{

struct ProviderSelection
{
  std::vector<std::string> ids;
  std::string normalized;

  bool contains(std::string_view id) const;
};

ProviderSelection parse_provider_selection(std::string_view value,
                                           std::span<const std::string_view> built_provider_ids);

} // namespace studio_remote
