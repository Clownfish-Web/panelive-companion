#include <studio_remote/teleprompter/script.hpp>

#include <algorithm>

namespace studio_remote::teleprompter
{
std::string_view playback_state_name(PlaybackState state) noexcept
{
  switch (state)
  {
  case PlaybackState::ready:
    return "ready";
  case PlaybackState::playing:
    return "playing";
  case PlaybackState::paused:
    return "paused";
  case PlaybackState::completed:
    return "completed";
  }
  return {};
}

std::optional<PlaybackState> parse_playback_state(std::string_view state) noexcept
{
  for (const auto candidate : {PlaybackState::ready,
                               PlaybackState::playing,
                               PlaybackState::paused,
                               PlaybackState::completed})
    if (playback_state_name(candidate) == state)
      return candidate;
  return std::nullopt;
}

Result<Delivery> accept_progress(const Script& script,
                                 const Delivery& current_delivery,
                                 const Progress& progress,
                                 std::uint64_t received_at_unix_ms)
{
  if (progress.revision != script.summary.revision ||
      current_delivery.retrieved_revision != progress.revision)
    return {{}, Error::conflict, "Retrieve the current script revision before reporting progress"};
  const auto& text = script.document.spoken_text;
  if (progress.sequence == 0 || progress.sequence > maximum_safe_integer ||
      progress.elapsed_ms > 86'400'000 || progress.spoken_text_byte_offset > text.size() ||
      playback_state_name(progress.state).empty() || received_at_unix_ms > maximum_safe_integer)
    return {{}, Error::invalid_progress, "Invalid playback progress"};
  if (progress.spoken_text_byte_offset < text.size() &&
      (static_cast<unsigned char>(
           text[static_cast<std::size_t>(progress.spoken_text_byte_offset)]) &
       0xc0) == 0x80)
    return {{}, Error::invalid_progress, "Playback position must be a UTF-8 character boundary"};
  if (progress.cue_id &&
      std::ranges::none_of(script.document.cues,
                           [&](const Cue& cue) { return cue.id == *progress.cue_id; }))
    return {{}, Error::invalid_progress, "Unknown script cue"};
  if (current_delivery.progress && progress.sequence <= current_delivery.progress->sequence)
  {
    if (progress == *current_delivery.progress)
      return {current_delivery, Error::none, {}};
    return {{}, Error::conflict, "Playback sequence is stale; reload script details"};
  }
  auto delivery = current_delivery;
  const auto timestamp = std::max(received_at_unix_ms, delivery.received_at_unix_ms.value_or(0));
  if (progress.cue_id && (!delivery.progress || delivery.progress->cue_id != progress.cue_id))
    delivery.cue_received_at_unix_ms = timestamp;
  if (!progress.cue_id)
    delivery.cue_received_at_unix_ms.reset();
  delivery.progress = progress;
  delivery.received_at_unix_ms = timestamp;
  return {std::move(delivery), Error::none, {}};
}
} // namespace studio_remote::teleprompter
