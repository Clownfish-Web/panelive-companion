#include "obs_audio_monitor.hpp"

#include "obs_audio_mix_policy.hpp"

#include <util/platform.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#ifdef _WIN32
#include <avrt.h>
#endif
#include <unordered_set>
#include <vector>

namespace studio_remote::obs_plugin
{
namespace
{
constexpr std::uint64_t refresh_interval_ns = 500'000'000ULL;
constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;

std::uint64_t sample_index_from_ns(std::uint64_t timestamp_ns)
{
  const auto seconds = timestamp_ns / nanoseconds_per_second;
  const auto remainder = timestamp_ns % nanoseconds_per_second;
  return seconds * ipc::audio_monitor_sample_rate +
         (remainder * ipc::audio_monitor_sample_rate + nanoseconds_per_second / 2) /
             nanoseconds_per_second;
}

std::uint64_t timestamp_from_sample_index(std::uint64_t sample_index)
{
  const auto seconds = sample_index / ipc::audio_monitor_sample_rate;
  const auto remainder = sample_index % ipc::audio_monitor_sample_rate;
  return seconds * nanoseconds_per_second +
         remainder * nanoseconds_per_second / ipc::audio_monitor_sample_rate;
}

std::uint64_t apply_sync_offset(std::uint64_t timestamp, std::int64_t offset)
{
  if (offset >= 0)
  {
    const auto positive = static_cast<std::uint64_t>(offset);
    return timestamp > std::numeric_limits<std::uint64_t>::max() - positive
               ? std::numeric_limits<std::uint64_t>::max()
               : timestamp + positive;
  }
  const auto negative = static_cast<std::uint64_t>(-(offset + 1)) + 1;
  return timestamp > negative ? timestamp - negative : 0;
}

} // namespace

ObsAudioMonitor::ObsAudioMonitor() = default;

ObsAudioMonitor::~ObsAudioMonitor()
{
  stop();
}

bool ObsAudioMonitor::start()
{
  if (started_)
    return true;
  if (!open_mapping())
    return false;
  const auto* info = audio_output_get_info(obs_get_audio());
  input_channels_ = info ? static_cast<std::uint32_t>(get_audio_channels(info->speakers)) : 0;
  if (!info || info->samples_per_sec != ipc::audio_monitor_sample_rate || input_channels_ == 0 ||
      input_channels_ > audio_ingress_maximum_channels)
  {
    close_mapping();
    return false;
  }
  stopping_.store(false, std::memory_order_release);
  obs_frontend_add_event_callback(handle_frontend_event, this);
  frontend_callback_registered_ = true;
  obs_add_main_render_callback(on_main_render, this);
  mixer_thread_ = std::thread([this] { mixer_loop(); });
  started_ = true;
  return true;
}

void ObsAudioMonitor::stop()
{
  if (!started_)
    return;
  if (frontend_callback_registered_)
    obs_frontend_remove_event_callback(handle_frontend_event, this);
  frontend_callback_registered_ = false;
  obs_remove_main_render_callback(on_main_render, this);
  stopping_.store(true, std::memory_order_release);
  detach_sources();
  if (mixer_thread_.joinable())
    mixer_thread_.join();
  mix_frames_.clear();
  close_mapping();
  started_ = false;
}

void ObsAudioMonitor::detach_sources()
{
  std::unordered_map<std::string, std::shared_ptr<ObsAudioSourceIngress>> retiring;
  {
    std::lock_guard lock(taps_mutex_);
    retiring.swap(taps_);
    for (auto& [_, tap] : retiring)
      tap->detach();
  }
  retiring.clear();
  if (mapping_)
  {
    mapping_->configured.store(0, std::memory_order_release);
    mapping_->channels.store(0, std::memory_order_release);
  }
}

void ObsAudioMonitor::on_main_render(void* context, std::uint32_t, std::uint32_t)
{
  if (context)
    static_cast<ObsAudioMonitor*>(context)->refresh_sources(os_gettime_ns());
}

void ObsAudioMonitor::handle_frontend_event(obs_frontend_event event, void* context)
{
  if (!context)
    return;
  auto& monitor = *static_cast<ObsAudioMonitor*>(context);
  switch (event)
  {
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
  case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
    monitor.release_sources_for_reload();
    break;
  case OBS_FRONTEND_EVENT_EXIT:
    monitor.release_sources_for_reload();
    // OBS destroys the frontend callback list after its EXIT notification and before module
    // unload, so a later removal would only log a warning.
    monitor.frontend_callback_registered_ = false;
    break;
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
  case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
    if (monitor.reload_barrier_.complete())
      monitor.last_refresh_ns_ = 0;
    break;
  default:
    break;
  }
}

void ObsAudioMonitor::release_sources_for_reload()
{
  if (!reload_barrier_.begin())
    return;
  detach_sources();
  blog(LOG_INFO, "[PaneLive Companion] Released audio source callbacks before OBS frontend reload");
}

void ObsAudioMonitor::refresh_sources(std::uint64_t now_ns)
{
  if (reload_barrier_.blocked())
    return;
  if (now_ns - last_refresh_ns_ < refresh_interval_ns)
    return;
  last_refresh_ns_ = now_ns;

  std::unordered_set<std::string> monitored;
  obs_enum_sources(
      [](void* context, obs_source_t* source)
      {
        if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) == 0 ||
            obs_source_get_monitoring_type(source) == OBS_MONITORING_TYPE_NONE)
          return true;
        const char* uuid = obs_source_get_uuid(source);
        if (uuid && *uuid)
          static_cast<std::unordered_set<std::string>*>(context)->emplace(uuid);
        return true;
      },
      &monitored);

  const bool activate_audition_sources =
      control_ && ipc::compatible_audio_monitor_control(*control_) &&
      control_->activate_audio_source_audition_bus.load(std::memory_order_acquire) != 0;

  std::lock_guard lock(taps_mutex_);
  bool topology_changed = false;
  for (auto iterator = taps_.begin(); iterator != taps_.end();)
  {
    if (monitored.contains(iterator->first))
    {
      ++iterator;
      continue;
    }
    iterator->second->detach();
    iterator = taps_.erase(iterator);
    topology_changed = true;
  }
  for (const auto& uuid : monitored)
  {
    if (taps_.contains(uuid))
      continue;
    obs_source_t* source = obs_get_source_by_uuid(uuid.c_str());
    if (!source)
      continue;
    auto tap = std::make_shared<ObsAudioSourceIngress>(
        source, static_cast<std::uint16_t>(input_channels_), ingress_counters_);
    tap->update_control(obs_source_get_volume(source),
                        obs_source_get_sync_offset(source),
                        topology_generation_.load(std::memory_order_relaxed) + 1);
    taps_.emplace(uuid, tap);
    tap->attach();
    tap->set_audition_activation(activate_audition_sources);
    topology_changed = true;
  }
  if (topology_changed)
    topology_generation_.fetch_add(1, std::memory_order_relaxed);
  for (auto& [_, tap] : taps_)
  {
    tap->set_audition_activation(activate_audition_sources);
    tap->update_control(obs_source_get_volume(tap->source()),
                        obs_source_get_sync_offset(tap->source()),
                        topology_generation_.load(std::memory_order_relaxed));
  }
  if (mapping_)
  {
    mapping_->configured.store(taps_.empty() ? 0U : 1U, std::memory_order_release);
    mapping_->channels.store(taps_.empty() ? 0U : ipc::audio_monitor_maximum_channels,
                             std::memory_order_release);
  }
}

void ObsAudioMonitor::mixer_loop()
{
#ifdef _WIN32
  DWORD task_index = 0;
  HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
  if (mmcss && !AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_NORMAL))
  {
    AvRevertMmThreadCharacteristics(mmcss);
    mmcss = nullptr;
  }
#endif
  while (!stopping_.load(std::memory_order_acquire))
  {
    const auto now = os_gettime_ns();
    if (mapping_)
    {
      mapping_->heartbeat_ns.store(now, std::memory_order_release);
      mapping_->ingress_overflow_drops.store(ingress_counters_.overflow_drops.load(),
                                             std::memory_order_relaxed);
      mapping_->oversized_callback_drops.store(ingress_counters_.oversized_callback_drops.load(),
                                               std::memory_order_relaxed);
      mapping_->callback_count.store(ingress_counters_.callback_count.load(),
                                     std::memory_order_relaxed);
      mapping_->callback_frames_le_256.store(ingress_counters_.callback_frame_histogram[0].load(),
                                             std::memory_order_relaxed);
      mapping_->callback_frames_le_512.store(ingress_counters_.callback_frame_histogram[1].load(),
                                             std::memory_order_relaxed);
      mapping_->callback_frames_le_1024.store(ingress_counters_.callback_frame_histogram[2].load(),
                                              std::memory_order_relaxed);
      mapping_->callback_frames_le_2048.store(ingress_counters_.callback_frame_histogram[3].load(),
                                              std::memory_order_relaxed);
      mapping_->maximum_ingress_occupancy.store(ingress_counters_.maximum_occupancy.load(),
                                                std::memory_order_relaxed);
      mapping_->dropped_frames.store(ingress_counters_.overflow_drops.load() +
                                         ingress_counters_.oversized_callback_drops.load(),
                                     std::memory_order_relaxed);
    }
    std::vector<std::shared_ptr<ObsAudioSourceIngress>> sources;
    {
      std::lock_guard lock(taps_mutex_);
      sources.reserve(taps_.size());
      for (const auto& [_, source] : taps_)
        sources.push_back(source);
    }
    for (const auto& source : sources)
      consume_source(*source);
    publish_due(now);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
#ifdef _WIN32
  if (mmcss)
    AvRevertMmThreadCharacteristics(mmcss);
#endif
}

void ObsAudioMonitor::consume_source(ObsAudioSourceIngress& source)
{
  while (const auto* block = source.try_begin_read())
  {
    mix_block(source, *block);
    source.commit_read();
  }
}

void ObsAudioMonitor::mix_block(ObsAudioSourceIngress& source, const ObsAudioIngressBlock& block)
{
  const auto normalized_timestamp =
      source.normalize_timestamp(block.callback_timestamp_ns, os_gettime_ns());
  const auto timestamp = apply_sync_offset(normalized_timestamp, block.sync_offset_ns);
  const auto timestamp_sample = sample_index_from_ns(timestamp);
  const auto placement = place_audio_block(timestamp_sample,
                                           source.expected_next_sample(),
                                           block.frame_count,
                                           source.timeline_jump_state());
  if (placement.discontinuity && mapping_)
  {
    mapping_->timestamp_discontinuities.fetch_add(1, std::memory_order_relaxed);
    if (placement.timeline_difference > 0)
      mapping_->timeline_gaps.fetch_add(static_cast<std::uint64_t>(placement.timeline_difference),
                                        std::memory_order_relaxed);
    else
      mapping_->timeline_overlaps.fetch_add(
          static_cast<std::uint64_t>(-placement.timeline_difference), std::memory_order_relaxed);
  }
  if (next_publish_sample_ == 0)
    next_publish_sample_ = placement.first_sample / ipc::audio_monitor_frame_samples *
                           ipc::audio_monitor_frame_samples;
  else if (placement.first_sample < next_publish_sample_)
    grow_publication_delay(next_publish_sample_ - placement.first_sample);

  std::array<const float*, audio_ingress_maximum_channels> channel_pointers{};
  for (std::size_t channel = 0; channel < block.channels; ++channel)
    channel_pointers[channel] = block.planar[channel].data();
  const std::span<const float* const> channels(channel_pointers.data(), block.channels);
  const auto gain = block.muted ? 0.0F : block.volume;
  for (std::uint32_t frame = placement.source_offset; frame < block.frame_count; ++frame)
  {
    const auto sample_index = placement.first_sample + frame - placement.source_offset;
    if (sample_index < next_publish_sample_)
      continue;
    const auto packet_sample =
        sample_index / ipc::audio_monitor_frame_samples * ipc::audio_monitor_frame_samples;
    auto& output = mix_frames_[packet_sample];
    if (!block.muted && source.mark_packet_contribution(packet_sample))
      ++output.active_source_count;
    const auto offset = static_cast<std::size_t>(sample_index - packet_sample);
    output.samples[offset * 2] += downmix_audio_sample(channels, frame, true) * gain;
    output.samples[offset * 2 + 1] += downmix_audio_sample(channels, frame, false) * gain;
    ++output.coverage[offset];
    output.overlap_repaired = output.overlap_repaired || placement.overlap_repaired;
    output.discontinuity = output.discontinuity || placement.discontinuity;
  }
  source.set_expected_next_sample(placement.next_expected_sample);
}

void ObsAudioMonitor::grow_publication_delay(std::uint64_t late_samples)
{
  const auto grown = grown_publication_delay_ns(publication_delay_ns_, late_samples);
  if (grown == publication_delay_ns_)
    return;
  blog(LOG_INFO,
       "[PaneLive Companion] Adding %llu ms of monitor audio buffering, total %llu ms",
       static_cast<unsigned long long>((grown - publication_delay_ns_) / 1'000'000ULL),
       static_cast<unsigned long long>(grown / 1'000'000ULL));
  publication_delay_ns_ = grown;
}

void ObsAudioMonitor::publish_due(std::uint64_t now_ns)
{
  if (next_publish_sample_ == 0 || now_ns <= publication_delay_ns_)
    return;
  const auto due_sample = sample_index_from_ns(now_ns - publication_delay_ns_);
  std::size_t source_count = 0;
  {
    std::lock_guard lock(taps_mutex_);
    source_count = taps_.size();
  }
  std::uint64_t published = 0;
  while (next_publish_sample_ + ipc::audio_monitor_frame_samples <= due_sample && published < 4)
  {
    auto found = mix_frames_.find(next_publish_sample_);
    MixFrame frame;
    if (found != mix_frames_.end())
    {
      frame = std::move(found->second);
      mix_frames_.erase(found);
    }
    publish(next_publish_sample_, std::move(frame), source_count);
    next_publish_sample_ += ipc::audio_monitor_frame_samples;
    ++published;
  }
  if (published == 4 && next_publish_sample_ + ipc::audio_monitor_frame_samples <= due_sample &&
      mapping_)
    mapping_->mixer_deadline_misses.fetch_add(1, std::memory_order_relaxed);
  while (!mix_frames_.empty() && mix_frames_.begin()->first < next_publish_sample_)
    mix_frames_.erase(mix_frames_.begin());
}

void ObsAudioMonitor::publish(std::uint64_t first_sample_index,
                              MixFrame frame,
                              std::size_t source_count)
{
  if (!mapping_)
    return;
  ipc::AudioMonitorFrameFlags flags = ipc::AudioMonitorFrameFlags::none;
  for (const auto coverage : frame.coverage)
  {
    if (coverage < source_count)
    {
      flags = static_cast<ipc::AudioMonitorFrameFlags>(
          static_cast<std::uint16_t>(flags) |
          static_cast<std::uint16_t>(ipc::AudioMonitorFrameFlags::gap));
      mapping_->timeline_gaps.fetch_add(1, std::memory_order_relaxed);
    }
    if (coverage > source_count)
    {
      flags = static_cast<ipc::AudioMonitorFrameFlags>(
          static_cast<std::uint16_t>(flags) |
          static_cast<std::uint16_t>(ipc::AudioMonitorFrameFlags::overlap_repaired));
      mapping_->timeline_overlaps.fetch_add(1, std::memory_order_relaxed);
    }
  }
  if (frame.overlap_repaired)
    flags = static_cast<ipc::AudioMonitorFrameFlags>(
        static_cast<std::uint16_t>(flags) |
        static_cast<std::uint16_t>(ipc::AudioMonitorFrameFlags::overlap_repaired));
  if (frame.discontinuity)
    flags = static_cast<ipc::AudioMonitorFrameFlags>(
        static_cast<std::uint16_t>(flags) |
        static_cast<std::uint16_t>(ipc::AudioMonitorFrameFlags::discontinuity));

  AudioMonitorMixSettings mix_settings;
  if (control_ && ipc::compatible_audio_monitor_control(*control_))
  {
    mix_settings.automatic_headroom =
        control_->automatic_headroom.load(std::memory_order_acquire) != 0;
    const auto attenuation_db =
        control_->attenuation_per_additional_source_db.load(std::memory_order_acquire);
    if (valid_audio_monitor_headroom_db(attenuation_db))
      mix_settings.attenuation_per_additional_source_db = attenuation_db;
  }
  const auto common_gain =
      update_audio_monitor_headroom(frame.active_source_count, mix_settings, headroom_state_);
  const auto metrics = finalize_audio_packet(frame.samples, common_gain, limiter_gain_);
  mapping_->non_finite_samples.fetch_add(metrics.non_finite_samples, std::memory_order_relaxed);
  if (metrics.limiter_active)
  {
    flags = static_cast<ipc::AudioMonitorFrameFlags>(
        static_cast<std::uint16_t>(flags) |
        static_cast<std::uint16_t>(ipc::AudioMonitorFrameFlags::limiter_active));
    mapping_->limiter_frames.fetch_add(1, std::memory_order_relaxed);
  }

  mapping_->latest_peak_millionths.store(
      static_cast<std::uint64_t>(std::llround(metrics.peak * 1'000'000.0)),
      std::memory_order_relaxed);
  mapping_->latest_rms_millionths.store(
      static_cast<std::uint64_t>(std::llround(metrics.rms * 1'000'000.0)),
      std::memory_order_relaxed);
  mapping_->latest_dc_millionths.store(
      static_cast<std::int64_t>(std::llround(metrics.dc * 1'000'000.0)), std::memory_order_relaxed);

  const auto sequence = ++sequence_;
  auto& slot = mapping_->slots[(sequence - 1) % ipc::audio_monitor_slot_count];
  slot.sequence.store(0, std::memory_order_release);
  slot.first_sample_index = first_sample_index;
  slot.captured_ns = timestamp_from_sample_index(first_sample_index);
  slot.topology_generation = topology_generation_.load(std::memory_order_relaxed);
  slot.sample_rate = ipc::audio_monitor_sample_rate;
  slot.channels = ipc::audio_monitor_maximum_channels;
  slot.frame_count = ipc::audio_monitor_frame_samples;
  slot.flags = flags;
  std::transform(frame.samples.begin(),
                 frame.samples.end(),
                 slot.samples,
                 [](double sample) { return static_cast<float>(sample); });
  slot.sequence.store(sequence, std::memory_order_release);
  mapping_->published_sequence.store(sequence, std::memory_order_release);
}

} // namespace studio_remote::obs_plugin
