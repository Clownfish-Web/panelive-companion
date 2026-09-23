#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::teleprompter
{
inline constexpr std::uint64_t maximum_safe_integer = 9'007'199'254'740'991;
inline constexpr std::size_t maximum_content_bytes = 262'144;
inline constexpr std::size_t maximum_scripts_per_device = 100;
inline constexpr std::size_t maximum_library_scripts = 1000;
inline constexpr std::size_t maximum_library_bytes = 64 * 1024 * 1024;

enum class Error
{
  none,
  invalid_script,
  invalid_progress,
  not_found,
  device_unavailable,
  control_denied,
  conflict,
  limit_reached,
  storage_unavailable
};

template <typename Value> struct Result
{
  std::optional<Value> value;
  Error error = Error::none;
  std::string message;

  explicit operator bool() const noexcept
  {
    return value.has_value();
  }
};

struct Draft
{
  std::string name;
  std::string content;
  std::uint32_t words_per_minute = 150;

  bool operator==(const Draft&) const = default;
};

enum class CueType
{
  bookmark,
  pause
};

struct Cue
{
  std::string id;
  CueType type = CueType::bookmark;
  std::string label;
  std::uint64_t spoken_text_byte_offset = 0;
  std::uint64_t estimated_at_ms = 0;
  std::uint32_t pause_ms = 0;
};

struct CompiledScript
{
  Draft draft;
  std::string spoken_text;
  std::uint64_t word_count = 0;
  std::uint64_t estimated_duration_ms = 0;
  std::vector<Cue> cues;
};

enum class PlaybackState
{
  ready,
  playing,
  paused,
  completed
};

struct Progress
{
  std::uint64_t revision = 0;
  std::uint64_t sequence = 0;
  PlaybackState state = PlaybackState::ready;
  std::uint64_t spoken_text_byte_offset = 0;
  std::uint64_t elapsed_ms = 0;
  std::optional<std::string> cue_id;

  bool operator==(const Progress&) const = default;
};

struct Delivery
{
  std::optional<std::uint64_t> retrieved_revision;
  std::optional<std::uint64_t> retrieved_at_unix_ms;
  std::optional<Progress> progress;
  std::optional<std::uint64_t> received_at_unix_ms;
  std::optional<std::uint64_t> cue_received_at_unix_ms;
};

struct Summary
{
  std::string id;
  std::string name;
  std::uint64_t revision = 1;
  std::uint64_t created_at_unix_ms = 0;
  std::uint64_t modified_at_unix_ms = 0;
  std::uint64_t word_count = 0;
  std::uint64_t estimated_duration_ms = 0;
};

struct Script
{
  Summary summary;
  CompiledScript document;
  Delivery delivery;
};

/// Validates UTF-8, normalizes line endings and compiles non-executable cue lines.
[[nodiscard]] Result<CompiledScript> compile_script(Draft draft);
/// Pure validation; does not infer playback or execute production operations.
[[nodiscard]] Result<Delivery> accept_progress(const Script& script,
                                               const Delivery& current_delivery,
                                               const Progress& progress,
                                               std::uint64_t received_at_unix_ms);
[[nodiscard]] std::string_view playback_state_name(PlaybackState state) noexcept;
[[nodiscard]] std::optional<PlaybackState> parse_playback_state(std::string_view state) noexcept;
} // namespace studio_remote::teleprompter
