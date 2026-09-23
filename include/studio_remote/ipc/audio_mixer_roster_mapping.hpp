#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace studio_remote::ipc
{

inline constexpr wchar_t audio_mixer_roster_mapping_name[] =
    L"Local\\panelive-companion.audio-mixer-roster.v1";
inline constexpr char audio_mixer_roster_shared_memory_name[] =
    "/clownfishweb-panelive-audio-mixer-roster-v1";
inline constexpr std::uint32_t audio_mixer_roster_magic = 0x41554D58;
inline constexpr std::uint32_t audio_mixer_roster_version = 1;
inline constexpr std::size_t maximum_audio_mixer_entries = 256;

enum AudioMixerMembershipReason : std::uint32_t
{
  audio_mixer_program = 1U << 0U,
  audio_mixer_global = 1U << 1U,
  audio_mixer_preview = 1U << 2U,
  audio_mixer_pinned = 1U << 3U,
  audio_mixer_inactive_visible = 1U << 4U,
  audio_mixer_hidden_visible = 1U << 5U
};

struct AudioMixerRosterEntry
{
  char input_uuid[129]{};
  char input_name[256]{};
  std::uint32_t membership_reasons = 0;
};

struct AudioMixerRosterMapping
{
  std::uint32_t magic = audio_mixer_roster_magic;
  std::uint32_t version = audio_mixer_roster_version;
  std::uint32_t structure_size = 0;
  std::uint32_t producer_process_id = 0;
  std::uint32_t entry_count = 0;
  std::uint32_t reserved = 0;
  AudioMixerRosterEntry entries[maximum_audio_mixer_entries]{};
  alignas(8) std::atomic<std::uint64_t> sequence{0};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

} // namespace studio_remote::ipc
