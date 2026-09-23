#pragma once

#include "../../core/application/tally_publication_service.hpp"

#include <nlohmann/json.hpp>

namespace studio_remote
{
enum class TallyRouteKind
{
  collection,
  publication,
  rotate,
  missing
};
struct TallyRoute
{
  TallyRouteKind kind{TallyRouteKind::missing};
  std::string publication_id;
};
struct TallyResponse
{
  int status;
  nlohmann::json body;
  std::uint32_t retry_after_ms{0};
  std::optional<std::string> changed_publication;
};

[[nodiscard]] TallyRoute tally_route(std::string_view path);
[[nodiscard]] TallyResponse read_tally_publications(tally::PublicationService& service,
                                                    std::string_view device,
                                                    const TallyRoute& route);
[[nodiscard]] TallyResponse put_tally_publication(tally::PublicationService& service,
                                                  std::string_view device,
                                                  std::string_view publication_id,
                                                  std::string_view body);
[[nodiscard]] TallyResponse delete_tally_publication(tally::PublicationService& service,
                                                     std::string_view device,
                                                     std::string_view publication_id,
                                                     std::string_view body);
[[nodiscard]] TallyResponse rotate_tally_link(tally::PublicationService& service,
                                              std::string_view device,
                                              std::string_view publication_id,
                                              std::string_view body);
} // namespace studio_remote
