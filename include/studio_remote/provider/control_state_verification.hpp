#pragma once

#include <studio_remote/common/types.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <type_traits>
#include <variant>

namespace studio_remote
{

using ControlStateValue = std::variant<bool, double, std::int64_t, std::string>;

struct ControlStateExpectation
{
  std::string control_id;
  std::string request_type;
  json request_data = json::object();
  std::string response_field;
  ControlStateValue expected_value;
  double numeric_tolerance = 0.0;
};

enum class ControlStateVerification
{
  matched,
  changed,
  unavailable
};

inline ControlStateVerification verify_control_response(const ControlStateExpectation& expectation,
                                                        const json& response)
{
  const auto value = response.find(expectation.response_field);
  if (value == response.end())
    return ControlStateVerification::unavailable;

  const bool matched = std::visit(
      [&](const auto& expected)
      {
        using Value = std::decay_t<decltype(expected)>;
        if constexpr (std::is_same_v<Value, bool>)
          return value->is_boolean() && value->get<bool>() == expected;
        else if constexpr (std::is_same_v<Value, double>)
          return value->is_number() && std::isfinite(value->get<double>()) &&
                 std::abs(value->get<double>() - expected) <= expectation.numeric_tolerance;
        else if constexpr (std::is_same_v<Value, std::int64_t>)
          return value->is_number_integer() && value->get<std::int64_t>() == expected;
        else
          return value->is_string() && value->get_ref<const std::string&>() == expected;
      },
      expectation.expected_value);
  return matched ? ControlStateVerification::matched : ControlStateVerification::changed;
}

} // namespace studio_remote
