#pragma once

#include <studio_remote/teleprompter/script.hpp>

#include <variant>

namespace studio_remote::teleprompter
{
enum class FontFamily
{
  noto_sans_medium,
  roboto_medium
};
[[nodiscard]] std::string_view font_family_name(FontFamily font) noexcept;
[[nodiscard]] std::optional<FontFamily> parse_font_family(std::string_view name) noexcept;

struct PresentationSettings
{
  std::uint32_t words_per_minute = 150;
  bool mirror_vertical = true;
  bool mirror_horizontal = false;
  std::uint32_t font_basis_points = 500;
  std::uint32_t start_basis_points = 5000;
  // Canonical width in lowercase-x advances; constrained only by the master viewport.
  double content_width_x = 48.0;
  // Applied by the master player; Companion never extrapolates playback.
  std::int32_t speed_trim_percent = 0;
  std::uint32_t line_spacing_basis_points = 14000;
  bool voice_follow_enabled = false;
  std::string speech_language = "en-US";
  FontFamily font_family = FontFamily::noto_sans_medium;
  bool operator==(const PresentationSettings&) const = default;
};

enum class SessionRole
{
  master,
  controller,
  monitor
};
[[nodiscard]] std::string_view session_role_name(SessionRole role) noexcept;
[[nodiscard]] std::optional<SessionRole> parse_session_role(std::string_view name) noexcept;

struct SessionMember
{
  std::string device_id;
  SessionRole role = SessionRole::monitor;
  bool operator==(const SessionMember&) const = default;
};

/// Local management selection, never a mobile-supplied owner override.
struct SessionAssignment
{
  std::vector<std::string> script_ids;
  std::vector<SessionMember> members;
  SessionRole owner_role = SessionRole::master;
};

/// A master viewport checkpoint. Time/version identify its accepted geometry and clock sample.
struct SessionLineAlignment
{
  std::uint64_t line_number = 0;
  std::string line_text;
  std::uint64_t line_start_byte_offset = 0;
  std::uint64_t line_end_byte_offset = 0;
  double lower_edge_line = 0.0;
  double upper_edge_line = 0.0;
  double lines_per_second = 0.0;
  std::uint64_t sampled_at_monotonic_ms = 0;
  std::uint64_t version = 0;
  bool operator==(const SessionLineAlignment&) const = default;
};

/// Actual local voice-follow state; controller playback intent remains separate.
enum class VoiceFollowPhase
{
  off,
  preparing,
  acquiring,
  tracking,
  uncertain,
  recentering,
  holding,
  paused,
  unavailable
};
[[nodiscard]] std::string_view voice_follow_phase_name(VoiceFollowPhase phase) noexcept;
[[nodiscard]] std::optional<VoiceFollowPhase>
parse_voice_follow_phase(std::string_view name) noexcept;
struct VoiceFollowState
{
  VoiceFollowPhase phase = VoiceFollowPhase::off;
  std::string reason;
  double lines_per_second = 0.0;
  double upper_edge_line = 0.0;
  double viewport_lines = 0.0;
  std::optional<double> startup_progress = std::nullopt;
  bool operator==(const VoiceFollowState&) const = default;
};
[[nodiscard]] bool valid_voice_follow_state(const VoiceFollowState& state, bool enabled) noexcept;

struct TimedPauseState
{
  std::uint64_t source_byte_offset = 0;
  double remaining_ms = 0;
  double upper_edge_line = 0;
  double viewport_lines = 0;
};
[[nodiscard]] bool valid_timed_pause(const TimedPauseState& state, std::string_view text) noexcept;

inline constexpr std::uint64_t speed_adjustment_lease_ms = 750;
struct SpeedAdjustmentState
{
  std::uint64_t epoch = 1;
  std::uint64_t sequence = 0;
  double displacement = 0;
  std::uint64_t expires_at_monotonic_ms = 0;
};
enum class VideoMonitorSource
{
  program,
  preview
};
struct VideoMonitorSettings
{
  bool enabled = false;
  VideoMonitorSource source = VideoMonitorSource::program;
  double backdrop_opacity = 0.4;
  bool operator==(const VideoMonitorSettings&) const = default;
};
struct SessionState
{
  std::string id;
  std::string provider;
  std::uint64_t version = 1;
  std::string script_id;
  std::uint64_t script_revision = 1;
  PresentationSettings settings;
  VideoMonitorSettings video_monitor;
  PlaybackState playback = PlaybackState::ready;
  std::uint64_t elapsed_ms = 0;
  std::uint64_t anchor_monotonic_ms = 0;
  std::string owner_device_id;
  double word_position = 0.0;
  std::uint64_t position_sequence = 0;
  std::uint64_t seek_version = 0;
  std::uint64_t seek_byte_offset = 0;
  double seek_line_offset = 0;
  std::optional<SessionLineAlignment> alignment = std::nullopt;
  std::optional<double> master_width_capacity = std::nullopt;
  VoiceFollowState voice_follow = {};
  std::optional<TimedPauseState> timed_pause = std::nullopt;
  bool force_continue = false;
  // A second deliberate Play bypasses only the current voice-startup gate.
  bool startup_bypass = false;
  SpeedAdjustmentState speed_adjustment;
  std::uint64_t applied_seek_version = 0;
};

struct SessionSnapshot
{
  SessionState state;
  std::string clock_id;
  std::uint64_t server_monotonic_ms = 0;
  std::uint64_t spoken_text_byte_offset = 0;
  SessionRole role = SessionRole::monitor;
  std::vector<Summary> scripts;
  std::vector<SessionMember> members;
};

/// Master reports are ordered independently of controller commands.
struct SessionPositionReport
{
  std::uint64_t expected_version = 0;
  std::uint64_t sequence = 0;
  std::uint64_t elapsed_ms = 0;
  double word_position = 0.0;
  PlaybackState playback = PlaybackState::ready;
  std::string clock_id = {};
  std::uint64_t sampled_at_monotonic_ms = 0;
  std::optional<SessionLineAlignment> alignment = std::nullopt;
  // Maximum fitting contentWidthX * fontBasisPoints, independent of font size for the selected
  // face.
  double master_width_capacity = 0.0;
  VoiceFollowState voice_follow = {};
  std::optional<TimedPauseState> timed_pause = std::nullopt;
  std::uint64_t applied_seek_version = 0;
};

struct PlaySession
{
  bool operator==(const PlaySession&) const = default;
};
struct PauseSession
{
  bool operator==(const PauseSession&) const = default;
};
struct ResetSession
{
  bool operator==(const ResetSession&) const = default;
};
struct SeekSession
{
  std::uint64_t spoken_text_byte_offset = 0;
  double line_offset = 0;
  bool operator==(const SeekSession&) const = default;
};
struct SetSessionSettings
{
  PresentationSettings settings;
  bool operator==(const SetSessionSettings&) const = default;
};
struct SelectSessionScript
{
  std::string script_id;
  std::uint64_t revision = 0;
  bool operator==(const SelectSessionScript&) const = default;
};
struct ForceContinueSession
{
  bool enabled = false;
  bool operator==(const ForceContinueSession&) const = default;
};
struct AdjustSessionSpeed
{
  std::uint64_t epoch = 0;
  std::uint64_t sequence = 0;
  double displacement = 0;
  std::uint64_t sampled_at_monotonic_ms = 0;
  bool operator==(const AdjustSessionSpeed&) const = default;
};
struct SetVideoMonitor
{
  VideoMonitorSettings settings;
  bool operator==(const SetVideoMonitor&) const = default;
};
using SessionAction = std::variant<PlaySession,
                                   PauseSession,
                                   ResetSession,
                                   SeekSession,
                                   SetSessionSettings,
                                   SelectSessionScript,
                                   ForceContinueSession,
                                   AdjustSessionSpeed,
                                   SetVideoMonitor>;
struct SessionCommand
{
  std::string command_id;
  std::uint64_t expected_version = 0;
  SessionAction action;
};
struct SessionCommandResult
{
  SessionSnapshot snapshot;
  std::uint64_t accepted_version = 0;
  bool duplicate = false;
};

struct TimelinePoint
{
  std::uint64_t byte_offset = 0;
  std::uint64_t at_ms = 0;
};
/// Script is already validated. Points include pause starts/ends and the terminal text boundary.
[[nodiscard]] std::vector<TimelinePoint> session_timeline(const CompiledScript& script,
                                                          std::uint32_t words_per_minute);
[[nodiscard]] std::vector<std::uint64_t> spoken_word_offsets(std::string_view valid_utf8);
[[nodiscard]] bool valid_settings(const PresentationSettings& settings) noexcept;
/// Reduces only width to fit known master capacity; returns whether canonical wrapping changed.
bool constrain_master_width(SessionState& state) noexcept;
[[nodiscard]] std::uint64_t session_position(const SessionState& state,
                                             const CompiledScript& script);
/// Called only after serialized membership, idempotency, version and selection checks.
[[nodiscard]] Result<SessionState> apply_session_action(SessionState state,
                                                        const CompiledScript& script,
                                                        const SessionAction& action,
                                                        std::uint64_t monotonic_ms);
/// Unambiguous typed command identity for process-lifetime duplicate detection, not a wire payload.
[[nodiscard]] std::string session_command_identity(const SessionCommand& command);
} // namespace studio_remote::teleprompter
