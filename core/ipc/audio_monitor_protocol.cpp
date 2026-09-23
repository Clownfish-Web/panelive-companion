#include <studio_remote/ipc/audio_monitor_protocol.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace studio_remote::ipc
{

std::optional<std::string> validate_audio_monitor_mapping(const AudioMonitorMapping& mapping)
{
  if (mapping.magic != audio_monitor_magic)
    return "wrong-magic";
  if (mapping.version != audio_monitor_version)
    return "wrong-version";
  if (mapping.structure_size != sizeof(AudioMonitorMapping))
    return "wrong-structure-size";
  if (mapping.sample_rate != audio_monitor_sample_rate)
    return "unsupported-sample-rate";
  const auto channels = mapping.channels.load(std::memory_order_acquire);
  if (channels != 0 && channels != audio_monitor_maximum_channels)
    return "invalid-channel-count";
  if (mapping.sample_format != AudioMonitorSampleFormat::float32_interleaved)
    return "unsupported-sample-format";
  return std::nullopt;
}

bool compatible_audio_monitor_pair(const AudioMonitorMapping& audio,
                                   const AudioMonitorControlMapping& control)
{
  return !validate_audio_monitor_mapping(audio) && compatible_audio_monitor_control(control) &&
         audio.producer_process_id != 0 && audio.producer_process_id == control.producer_process_id;
}

bool publish_audio_monitor_settings(const AudioMonitorMapping& audio,
                                    AudioMonitorControlMapping& control,
                                    const AudioMonitorMixSettings& settings)
{
  if (!valid_audio_monitor_headroom_db(settings.attenuation_per_additional_source_db))
    throw std::invalid_argument("Audio monitor headroom preset is invalid");
  if (!compatible_audio_monitor_pair(audio, control))
    return false;
  control.automatic_headroom.store(settings.automatic_headroom, std::memory_order_release);
  control.attenuation_per_additional_source_db.store(settings.attenuation_per_additional_source_db,
                                                     std::memory_order_release);
  control.activate_audio_source_audition_bus.store(settings.activate_audio_source_audition_bus,
                                                   std::memory_order_release);
  return true;
}

AudioMonitorSequenceWindow pending_audio_monitor_sequences(const AudioMonitorMapping& mapping,
                                                           std::uint64_t after_sequence)
{
  if (validate_audio_monitor_mapping(mapping))
    return {};
  const auto published = mapping.published_sequence.load(std::memory_order_acquire);
  if (published == 0 || published <= after_sequence ||
      after_sequence == std::numeric_limits<std::uint64_t>::max())
    return {};

  const auto oldest_available =
      published > audio_monitor_slot_count ? published - audio_monitor_slot_count + 1 : 1;
  const auto requested = after_sequence + 1;
  const auto first = std::max(requested, oldest_available);
  return {first, published, first - requested};
}

std::optional<AudioMonitorFrame> copy_audio_monitor_frame(const AudioMonitorMapping& mapping,
                                                          std::uint64_t sequence)
{
  if (validate_audio_monitor_mapping(mapping) || sequence == 0)
    return std::nullopt;
  const auto published = mapping.published_sequence.load(std::memory_order_acquire);
  if (sequence > published || published - sequence >= audio_monitor_slot_count)
    return std::nullopt;
  const auto& slot = mapping.slots[(sequence - 1) % audio_monitor_slot_count];
  const auto before = slot.sequence.load(std::memory_order_acquire);
  if (before != sequence || slot.sample_rate != audio_monitor_sample_rate ||
      slot.channels != audio_monitor_maximum_channels ||
      slot.frame_count != audio_monitor_frame_samples)
    return std::nullopt;

  AudioMonitorFrame frame;
  frame.sample_rate = mapping.sample_rate;
  frame.channels = slot.channels;
  frame.sequence = sequence;
  frame.first_sample_index = slot.first_sample_index;
  frame.captured_ns = slot.captured_ns;
  frame.topology_generation = slot.topology_generation;
  frame.flags = static_cast<std::uint16_t>(slot.flags);
  const auto value_count = static_cast<std::size_t>(slot.frame_count) * slot.channels;
  frame.samples.assign(slot.samples, slot.samples + value_count);
  if (slot.sequence.load(std::memory_order_acquire) != before)
    return std::nullopt;
  return frame;
}

bool audio_monitor_heartbeat_fresh(std::uint64_t observed_ns,
                                   std::uint64_t now_ns,
                                   std::uint64_t timeout_ns)
{
  return observed_ns != 0 && now_ns >= observed_ns && now_ns - observed_ns < timeout_ns;
}

} // namespace studio_remote::ipc
