#pragma once

#include <studio_remote/tally/definition.hpp>

#include <cstdint>
#include <optional>
#include <set>

namespace studio_remote::tally
{
enum class CountdownOperation
{
  studio_transition,
  start_outputs
};
enum class CountdownPhase
{
  running,
  go
};

struct Countdown
{
  std::string id;
  CountdownOperation operation{CountdownOperation::studio_transition};
  CountdownPhase phase{CountdownPhase::running};
  std::int64_t remaining_ms{0};
  std::int64_t go_remaining_ms{0};
  bool recording{false};
  bool streaming{false};
  std::optional<Selection> target_scene;
  bool operator==(const Countdown&) const = default;
};

struct SourcePresence
{
  Selection source;
  std::set<std::string> active_scene_ids;
};

// Immutable, reconciled observations supplied by the application owner. No native OBS identifiers.
struct Signals
{
  bool connected{false};
  std::optional<Selection> program;
  std::optional<Selection> preview;
  bool preview_available{false};
  bool recording{false};
  bool streaming{false};
  std::vector<Selection> available_scenes;
  std::vector<SourcePresence> source_catalog;
  bool source_catalog_ready{false};
};

struct Resolution
{
  State state{State::disconnected};
  bool preview_also{false};
  std::optional<Countdown> countdown;
  std::optional<Countdown> next_countdown;
  std::vector<Selection> unavailable_scenes;
  std::vector<Selection> unavailable_sources;
};

struct SourceScene
{
  Selection scene;
  std::vector<Selection> sources;
};

// Countdown arguments contain accepted, unexpired display lifecycles only, author first.
[[nodiscard]] Resolution resolve(const Definition& definition,
                                 const Signals& signals,
                                 const std::optional<Countdown>& author_countdown = std::nullopt,
                                 const std::optional<Countdown>& other_countdown = std::nullopt);
[[nodiscard]] std::optional<SourceScene>
source_scene(const Definition& definition, const Signals& signals, const Resolution& resolution);
} // namespace studio_remote::tally
