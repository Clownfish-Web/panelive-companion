#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace studio_remote
{
using json = nlohmann::json;

enum class StateSequenceValidation
{
  accepted,
  missing_or_invalid,
  stale
};

inline StateSequenceValidation validate_expected_state_sequence(const json& envelope,
                                                                std::uint64_t current_sequence)
{
  const auto expected = envelope.find("expectedStateSequence");
  if (expected == envelope.end() || !expected->is_number_integer())
    return StateSequenceValidation::missing_or_invalid;

  if (expected->is_number_unsigned())
    return expected->get<std::uint64_t>() == current_sequence ? StateSequenceValidation::accepted
                                                              : StateSequenceValidation::stale;

  const auto signed_expected = expected->get<std::int64_t>();
  if (signed_expected < 0)
    return StateSequenceValidation::missing_or_invalid;

  return static_cast<std::uint64_t>(signed_expected) == current_sequence
             ? StateSequenceValidation::accepted
             : StateSequenceValidation::stale;
}

inline json asynchronous_command_rejection(const json& failure,
                                           std::uint64_t current_state_sequence)
{
  json rejection = {{"type", "commandRejected"},
                    {"commandId", failure.value("commandId", "")},
                    {"operationId", failure.value("operationId", "")},
                    {"reason", failure.value("reasonCode", "backend-unavailable")}};
  if (rejection["reason"] == "resource-stale")
    rejection["currentStateSequence"] = current_state_sequence;
  return rejection;
}

} // namespace studio_remote
