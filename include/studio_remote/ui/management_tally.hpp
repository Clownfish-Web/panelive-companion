#pragma once

#include <studio_remote/tally/publication.hpp>

#include <functional>
#include <string_view>

namespace studio_remote
{
// Trusted local management only. Mutation callbacks commit before refreshing client capabilities.
struct RemoteTallyManagementCallbacks
{
  std::function<tally::PublicationResult<bool>(std::string_view)> permission;
  std::function<tally::PublicationResult<bool>(std::string_view)> allow;
  std::function<tally::PublicationResult<bool>(std::string_view)> withdraw;
};
} // namespace studio_remote
