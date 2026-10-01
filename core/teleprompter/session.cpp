#include <studio_remote/teleprompter/session.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <utility>

namespace studio_remote::teleprompter
{
namespace
{
std::uint64_t word_time(std::size_t words, std::uint32_t speed)
{
  return (static_cast<std::uint64_t>(words) * 60'000 + speed - 1) / speed;
}

std::uint64_t seek_time(const std::vector<TimelinePoint>& timeline, std::uint64_t position)
{
  // First occurrence preserves a timed pause at the sought anchor.
  const auto after = std::ranges::upper_bound(timeline, position, {}, &TimelinePoint::byte_offset);
  const auto anchor =
      after == timeline.begin() ? timeline.front().byte_offset : std::prev(after)->byte_offset;
  return std::ranges::lower_bound(timeline, anchor, {}, &TimelinePoint::byte_offset)->at_ms;
}

Result<SessionState> invalid()
{
  return {{}, Error::invalid_progress, "Invalid shared teleprompter command"};
}
} // namespace

std::string_view font_family_name(FontFamily font) noexcept
{
  switch (font)
  {
  case FontFamily::noto_sans_medium:
    return "noto-sans-medium";
  case FontFamily::roboto_medium:
    return "roboto-medium";
  }
  return {};
}

std::optional<FontFamily> parse_font_family(std::string_view name) noexcept
{
  for (const auto font : {FontFamily::noto_sans_medium, FontFamily::roboto_medium})
    if (font_family_name(font) == name)
      return font;
  return std::nullopt;
}

bool valid_settings(const PresentationSettings& settings) noexcept
{
  return !font_family_name(settings.font_family).empty() && settings.speech_language == "en-US" &&
         settings.line_spacing_basis_points >= 10000 &&
         settings.line_spacing_basis_points <= 30000 && settings.words_per_minute >= 40 &&
         settings.words_per_minute <= 400 && settings.speed_trim_percent >= -100 &&
         settings.speed_trim_percent <= 100 && settings.font_basis_points >= 100 &&
         settings.font_basis_points <= 2000 && settings.start_basis_points <= 10000 &&
         std::isfinite(settings.content_width_x) && settings.content_width_x >= 1.0 &&
         settings.content_width_x <= 200.0;
}

std::string_view voice_follow_phase_name(VoiceFollowPhase phase) noexcept
{
  switch (phase)
  {
  case VoiceFollowPhase::off:
    return "off";
  case VoiceFollowPhase::preparing:
    return "preparing";
  case VoiceFollowPhase::acquiring:
    return "acquiring";
  case VoiceFollowPhase::tracking:
    return "tracking";
  case VoiceFollowPhase::uncertain:
    return "uncertain";
  case VoiceFollowPhase::recentering:
    return "recentering";
  case VoiceFollowPhase::holding:
    return "holding";
  case VoiceFollowPhase::paused:
    return "paused";
  case VoiceFollowPhase::unavailable:
    return "unavailable";
  }
  return {};
}

std::optional<VoiceFollowPhase> parse_voice_follow_phase(std::string_view name) noexcept
{
  for (const auto phase : {VoiceFollowPhase::off,
                           VoiceFollowPhase::preparing,
                           VoiceFollowPhase::acquiring,
                           VoiceFollowPhase::tracking,
                           VoiceFollowPhase::uncertain,
                           VoiceFollowPhase::recentering,
                           VoiceFollowPhase::holding,
                           VoiceFollowPhase::paused,
                           VoiceFollowPhase::unavailable})
    if (voice_follow_phase_name(phase) == name)
      return phase;
  return std::nullopt;
}

bool valid_voice_follow_state(const VoiceFollowState& state, bool enabled) noexcept
{
  const bool reason_valid =
      state.reason.empty() || state.reason == "permission" || state.reason == "model" ||
      state.reason == "language" || state.reason == "busy" || state.reason == "audio" ||
      state.reason == "decoder" || state.reason == "overrun" || state.reason == "foreground";
  const bool startup_valid =
      !state.startup_progress ||
      (enabled && std::isfinite(*state.startup_progress) && *state.startup_progress >= 0 &&
       *state.startup_progress <= 1 && state.lines_per_second == 0 &&
       (state.phase == VoiceFollowPhase::preparing || state.phase == VoiceFollowPhase::acquiring ||
        state.phase == VoiceFollowPhase::unavailable));
  return startup_valid && reason_valid && !voice_follow_phase_name(state.phase).empty() &&
         (enabled ? state.phase != VoiceFollowPhase::off : state.phase == VoiceFollowPhase::off) &&
         std::isfinite(state.lines_per_second) &&
         state.lines_per_second >= (state.phase == VoiceFollowPhase::recentering ? -100 : 0) &&
         state.lines_per_second <= 100 && std::isfinite(state.upper_edge_line) &&
         std::abs(state.upper_edge_line) <= maximum_content_bytes + 10000 &&
         std::isfinite(state.viewport_lines) && state.viewport_lines >= 0 &&
         state.viewport_lines <= 10000 &&
         ((state.phase != VoiceFollowPhase::holding && state.phase != VoiceFollowPhase::paused) ||
          state.lines_per_second == 0);
}

bool constrain_master_width(SessionState& state) noexcept
{
  if (!state.master_width_capacity)
    return false;
  const auto available = *state.master_width_capacity / state.settings.font_basis_points;
  if (state.settings.content_width_x <= available)
    return false;
  const auto maximum_width = std::max(1.0, std::floor(available));
  const auto width = std::min(state.settings.content_width_x, maximum_width);
  if (width == state.settings.content_width_x)
    return false;
  state.settings.content_width_x = width;
  return true;
}

std::vector<TimelinePoint> session_timeline(const CompiledScript& script, std::uint32_t speed)
{
  const auto words = spoken_word_offsets(script.spoken_text);
  std::vector<TimelinePoint> result;
  std::uint64_t pauses = 0;
  std::size_t cue_index = 0;
  for (std::size_t index = 0; index <= words.size(); ++index)
  {
    const auto position = index < words.size() ? words[index] : script.spoken_text.size();
    const auto reading = word_time(index, speed);
    while (cue_index < script.cues.size() &&
           script.cues[cue_index].spoken_text_byte_offset <= position)
    {
      const auto& cue = script.cues[cue_index++];
      result.push_back({cue.spoken_text_byte_offset, reading + pauses});
      pauses += cue.pause_ms;
      result.push_back({cue.spoken_text_byte_offset, reading + pauses});
    }
    result.push_back({position, reading + pauses});
  }
  return result;
}

std::string_view session_role_name(SessionRole role) noexcept
{
  switch (role)
  {
  case SessionRole::master:
    return "master";
  case SessionRole::controller:
    return "controller";
  case SessionRole::monitor:
    return "monitor";
  }
  return {};
}

std::optional<SessionRole> parse_session_role(std::string_view name) noexcept
{
  for (const auto role : {SessionRole::master, SessionRole::controller, SessionRole::monitor})
    if (session_role_name(role) == name)
      return role;
  return std::nullopt;
}

std::uint64_t session_position(const SessionState& state, const CompiledScript& script)
{
  const auto words = spoken_word_offsets(script.spoken_text);
  if (state.word_position >= static_cast<double>(words.size()))
    return script.spoken_text.size();
  return words.empty() ? 0 : words[static_cast<std::size_t>(state.word_position)];
}

bool valid_timed_pause(const TimedPauseState& state, std::string_view text) noexcept
{
  return state.source_byte_offset < text.size() &&
         text.substr(state.source_byte_offset).starts_with("[") &&
         std::isfinite(state.remaining_ms) && state.remaining_ms > 0 &&
         state.remaining_ms <= 60000 && std::isfinite(state.upper_edge_line) &&
         std::abs(state.upper_edge_line) <= 272144 && std::isfinite(state.viewport_lines) &&
         state.viewport_lines > 0 && state.viewport_lines <= 10000;
}

Result<SessionState> apply_session_action(SessionState state,
                                          const CompiledScript& script,
                                          const SessionAction& action,
                                          std::uint64_t monotonic_ms)
{
  // Command timing never advances the master-owned reading position.
  if (std::holds_alternative<PauseSession>(action) ||
      std::holds_alternative<ResetSession>(action) || std::holds_alternative<SeekSession>(action) ||
      std::holds_alternative<SetSessionSettings>(action))
    state.speed_adjustment = {state.speed_adjustment.epoch + 1, 0, 0, 0};
  if (std::holds_alternative<PlaySession>(action))
  {
    if (state.playback != PlaybackState::completed)
    {
      state.startup_bypass = state.playback == PlaybackState::playing;
      state.playback = PlaybackState::playing;
    }
  }
  else if (std::holds_alternative<PauseSession>(action))
  {
    state.playback = PlaybackState::paused;
    state.startup_bypass = false;
  }
  else if (std::holds_alternative<ResetSession>(action))
  {
    state.playback = PlaybackState::ready;
    state.elapsed_ms = 0;
    state.word_position = 0.0;
    state.seek_version = state.version + 1;
    state.seek_byte_offset = 0;
    state.seek_line_offset = 0;
    state.timed_pause.reset();
    state.force_continue = false;
    state.startup_bypass = false;
  }
  else if (const auto* seek = std::get_if<SeekSession>(&action))
  {
    const auto position = seek->spoken_text_byte_offset;
    const auto& text = script.spoken_text;
    if (!std::isfinite(seek->line_offset) || seek->line_offset < 0 || seek->line_offset > 272144 ||
        position > text.size() ||
        (position < text.size() && (static_cast<unsigned char>(text[position]) & 0xc0) == 0x80))
      return invalid();
    state.elapsed_ms =
        seek_time(session_timeline(script, state.settings.words_per_minute), position);
    state.seek_version = state.version + 1;
    state.seek_byte_offset = position;
    state.seek_line_offset = seek->line_offset;
    state.startup_bypass = false;
    state.timed_pause.reset();
    if (state.playback == PlaybackState::completed)
      state.playback = PlaybackState::paused;
  }
  else if (const auto* settings = std::get_if<SetSessionSettings>(&action))
  {
    if (!valid_settings(settings->settings))
      return invalid();
    if (state.settings.voice_follow_enabled != settings->settings.voice_follow_enabled)
      state.startup_bypass = false;
    if (state.settings.font_family != settings->settings.font_family)
      state.master_width_capacity.reset();
    state.settings = settings->settings;
    constrain_master_width(state);
  }
  else if (const auto* video = std::get_if<SetVideoMonitor>(&action))
  {
    if (state.provider != "obs" && state.provider != "vmix" && state.provider != "streamlabs")
      return invalid();
    if (video->settings.source != VideoMonitorSource::program &&
        video->settings.source != VideoMonitorSource::preview)
      return invalid();
    if (!std::isfinite(video->settings.backdrop_opacity) || video->settings.backdrop_opacity < 0 ||
        video->settings.backdrop_opacity > 0.8)
      return invalid();
    state.video_monitor = video->settings;
  }
  else if (const auto* force = std::get_if<ForceContinueSession>(&action))
    state.force_continue = force->enabled;
  else if (const auto* speed = std::get_if<AdjustSessionSpeed>(&action))
  {
    const auto& previous = state.speed_adjustment;
    if (speed->epoch != previous.epoch || speed->sequence <= previous.sequence ||
        speed->sequence > 9007199254740991ULL ||
        speed->sampled_at_monotonic_ms > 9007199254740991ULL ||
        !std::isfinite(speed->displacement) || std::abs(speed->displacement) > 1 ||
        speed->sampled_at_monotonic_ms > monotonic_ms + 250 ||
        monotonic_ms > speed->sampled_at_monotonic_ms + speed_adjustment_lease_ms ||
        (speed->displacement != 0 && state.playback != PlaybackState::playing))
      return invalid();
    state.speed_adjustment = {
        speed->epoch,
        speed->sequence,
        speed->displacement,
        speed->displacement == 0 ? 0 : speed->sampled_at_monotonic_ms + speed_adjustment_lease_ms};
  }
  else
    return invalid(); // Selection requires repository script authorization before resetting state.
  return {std::move(state), Error::none, {}};
}

std::string session_command_identity(const SessionCommand& command)
{
  auto result =
      std::to_string(command.expected_version) + ":" + std::to_string(command.action.index());
  if (const auto* seek = std::get_if<SeekSession>(&command.action))
    result += ":" + std::to_string(seek->spoken_text_byte_offset) + ":" +
              std::to_string(std::bit_cast<std::uint64_t>(seek->line_offset));
  if (const auto* select = std::get_if<SelectSessionScript>(&command.action))
    result += ":" + std::to_string(select->revision) + ":" + select->script_id;
  if (const auto* setting = std::get_if<SetSessionSettings>(&command.action))
  {
    const auto& value = setting->settings;
    for (const auto number : {value.words_per_minute,
                              static_cast<std::uint32_t>(value.mirror_vertical),
                              static_cast<std::uint32_t>(value.mirror_horizontal),
                              value.font_basis_points,
                              value.start_basis_points,
                              value.line_spacing_basis_points})
      result += ":" + std::to_string(number);
    // Preserve every accepted fractional bit: decimal truncation would alias distinct commands.
    result += ":" + std::to_string(std::bit_cast<std::uint64_t>(value.content_width_x));
    result += ":" + std::string(font_family_name(value.font_family));
    result += ":" + std::to_string(value.speed_trim_percent);
    result += ":" + std::to_string(value.voice_follow_enabled) + ":" + value.speech_language;
  }
  if (const auto* video = std::get_if<SetVideoMonitor>(&command.action))
    result += ":" + std::to_string(video->settings.enabled) + ":" +
              std::to_string(static_cast<int>(video->settings.source)) + ":" +
              std::to_string(std::bit_cast<std::uint64_t>(video->settings.backdrop_opacity));
  if (const auto* force = std::get_if<ForceContinueSession>(&command.action))
    result += ":" + std::to_string(force->enabled);
  if (const auto* speed = std::get_if<AdjustSessionSpeed>(&command.action))
    result += ":" + std::to_string(speed->epoch) + ":" + std::to_string(speed->sequence) + ":" +
              std::to_string(std::bit_cast<std::uint64_t>(speed->displacement)) + ":" +
              std::to_string(speed->sampled_at_monotonic_ms);
  return result;
}
} // namespace studio_remote::teleprompter
