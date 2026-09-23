#pragma once

#include <nlohmann/json_fwd.hpp>
#include <studio_remote/tally/definition.hpp>
#include <studio_remote/tally/descriptor.hpp>
#include <studio_remote/tally/publication.hpp>
#include <studio_remote/tally/service_health.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace studio_remote::remote_tally_protocol
{
inline constexpr std::uint64_t maximum_revision = tally::maximum_revision;
inline constexpr std::size_t maximum_request_bytes = 64 * 1024;

struct PutRequest
{
  std::string operation_id;
  std::uint64_t expected_revision{0};
  std::string client_profile_id;
  tally::Definition definition;
};

struct MutationRequest
{
  std::string operation_id;
  std::uint64_t expected_revision{0};
};

struct Error
{
  int status{400};
  std::string field;
  std::string reason;
};

[[nodiscard]] bool valid_uuid_v4(std::string_view value);
[[nodiscard]] std::variant<PutRequest, Error> parse_put(std::string_view body);
[[nodiscard]] std::variant<MutationRequest, Error> parse_mutation(std::string_view body);
[[nodiscard]] std::variant<tally::Definition, Error> parse_definition(const nlohmann::json& body);
[[nodiscard]] nlohmann::json definition_json(const tally::Definition& definition);
[[nodiscard]] nlohmann::json render_config_json(const tally::Definition& definition);
[[nodiscard]] nlohmann::json descriptor_json(const tally::PublicationDescriptor& descriptor);
[[nodiscard]] tally::ServiceHealth
parse_health(unsigned status, std::string_view content_type, std::string_view body);
} // namespace studio_remote::remote_tally_protocol
