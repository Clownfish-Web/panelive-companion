#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace studio_remote
{
enum class CountdownStage
{
  started,
  go,
  cancelled
};
enum class CountdownOperation
{
  studio_transition,
  start_outputs
};

struct CountdownContext
{
  std::string profile_id;
  std::uint64_t session_generation = 0;
  std::optional<std::string> preview_composition_id;
  std::optional<std::string> preview_composition_name;
  std::optional<std::string> program_composition_id;
  std::optional<std::uint64_t> observed_transition_sequence;
  std::vector<std::string> streaming_target_ids;
  bool operator==(const CountdownContext&) const = default;
};

struct CountdownEvent
{
  std::string event_id;
  std::string countdown_id;
  CountdownStage stage = CountdownStage::started;
  CountdownOperation operation = CountdownOperation::studio_transition;
  std::uint32_t duration_seconds = 10;
  bool recording = false;
  bool streaming = false;
  CountdownContext context;
  std::optional<std::string> reason;
  bool operator==(const CountdownEvent&) const = default;
};

struct CountdownPermissions
{
  bool transition = false;
  bool recording = false;
  bool streaming = false;
};
} // namespace studio_remote
