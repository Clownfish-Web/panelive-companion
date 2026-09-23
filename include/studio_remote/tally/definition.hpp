#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::tally
{
enum class Mode
{
  flood,
  dot,
  bar,
  operator_view
};
enum class Binding
{
  scene,
  source,
  door,
  all
};
enum class Timer
{
  time_in_state,
  recording,
  time_of_day
};
enum class State
{
  off,
  preview,
  program,
  recording,
  streaming,
  standby,
  countdown,
  next,
  silence,
  live,
  disconnected,
  scene_unavailable,
  source_unavailable,
  source_data_unavailable
};

struct Selection
{
  std::string id;
  std::string name;
  bool operator==(const Selection&) const = default;
};

// Semantic publication data only. Native IDs and device presentation preferences never belong here.
struct Definition
{
  std::string id;
  std::string label;
  Mode mode{Mode::flood};
  Binding binding{Binding::door};
  std::vector<Selection> scenes;
  std::vector<Selection> sources;
  std::map<State, std::string> labels;
  bool show_secondary_line{true};
  bool standby{false};
  bool advanced_countdowns{true};
  Timer timer{Timer::time_in_state};
  bool operator==(const Definition&) const = default;
};

struct ValidationError
{
  std::string field;
  std::string reason;
  bool operator==(const ValidationError&) const = default;
};

// Counts Unicode scalar values, rejects malformed UTF-8 and C0/C1 control characters.
[[nodiscard]] bool valid_text(std::string_view text, std::size_t limit, bool allow_empty = false);
[[nodiscard]] std::optional<ValidationError> validate(const Definition& definition);
} // namespace studio_remote::tally
