#pragma once

#include <nlohmann/json.hpp>
#include <studio_remote/teleprompter/script.hpp>

namespace studio_remote::teleprompter_wire
{
nlohmann::json summary_json(const teleprompter::Summary& summary);
nlohmann::json script_json(const teleprompter::Script& script);
bool valid_id(std::string_view id);
std::optional<std::uint64_t> integer(const nlohmann::json& body, std::string_view field);
} // namespace studio_remote::teleprompter_wire
