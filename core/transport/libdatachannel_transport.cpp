#include "libdatachannel_transport.hpp"
#include "../../platform/shared/media/vp8_video_encoder.hpp"
#include "audio_nack_responder.hpp"
#include "audio_rtcp_feedback_handler.hpp"
#include "libdatachannel_peer.hpp"
#include "vp8_rtp_packetizer.hpp"
#include "webrtc_pending_offer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <rtc/rtp.hpp>
#include <rtc/rtppacketizer.hpp>
#include <span>
#include <studio_remote/credentials/credential_store.hpp>
#include <studio_remote/ipc/audio_monitor_mapping.hpp>
#include <studio_remote/media/frame_cadence.hpp>

#if !RTC_ENABLE_MEDIA
#error "PaneLive Companion requires libdatachannel media support; enable the vcpkg srtp feature"
#endif

namespace studio_remote
{
namespace
{
class LibDataChannelProcessLifetime
{
public:
  void acquire()
  {
    std::lock_guard lock(mutex_);
    if (owners_ != 0)
    {
      ++owners_;
      return;
    }
    const auto hardware_threads = std::max(2U, std::thread::hardware_concurrency());
    rtc::SetThreadPoolSize(std::min(4U, hardware_threads));
    rtc::InitLogger(rtc::LogLevel::Error);
    owners_ = 1;
  }

  void release()
  {
    std::lock_guard lock(mutex_);
    if (owners_ == 0)
      return;
    if (--owners_ == 0)
      rtc::Cleanup();
  }

private:
  std::mutex mutex_;
  std::size_t owners_ = 0;
};

LibDataChannelProcessLifetime& libdatachannel_process_lifetime()
{
  static LibDataChannelProcessLifetime lifetime;
  return lifetime;
}

std::string_view peer_state_name(rtc::PeerConnection::State state)
{
  switch (state)
  {
  case rtc::PeerConnection::State::New:
    return "new";
  case rtc::PeerConnection::State::Connecting:
    return "connecting";
  case rtc::PeerConnection::State::Connected:
    return "connected";
  case rtc::PeerConnection::State::Disconnected:
    return "disconnected";
  case rtc::PeerConnection::State::Failed:
    return "failed";
  case rtc::PeerConnection::State::Closed:
    return "closed";
  }
  return "unknown";
}

std::string_view gathering_state_name(rtc::PeerConnection::GatheringState state)
{
  switch (state)
  {
  case rtc::PeerConnection::GatheringState::New:
    return "new";
  case rtc::PeerConnection::GatheringState::InProgress:
    return "in-progress";
  case rtc::PeerConnection::GatheringState::Complete:
    return "complete";
  }
  return "unknown";
}
uint32_t rtp_timestamp_from_ns(std::uint64_t timestamp_ns)
{
  const auto seconds = timestamp_ns / 1'000'000'000ULL;
  const auto remainder = timestamp_ns % 1'000'000'000ULL;
  return static_cast<std::uint32_t>(seconds * 90'000ULL + remainder * 90'000ULL / 1'000'000'000ULL);
}

class VideoRtpTimeline
{
public:
  std::uint32_t map(std::uint64_t source_ns,
                    std::uint64_t source_generation,
                    std::uint32_t nominal_duration) noexcept
  {
    const auto source_timestamp = rtp_timestamp_from_ns(source_ns);
    if (!started_)
    {
      started_ = true;
      source_generation_ = source_generation;
      last_timestamp_ = source_timestamp;
      return last_timestamp_;
    }

    if (source_generation_ != source_generation)
    {
      source_generation_ = source_generation;
      offset_ = last_timestamp_ + nominal_duration - source_timestamp;
    }

    auto timestamp = source_timestamp + offset_;
    const auto forward_distance = timestamp - last_timestamp_;
    if (forward_distance == 0 || forward_distance >= 0x80000000U)
      timestamp = last_timestamp_ + nominal_duration;
    last_timestamp_ = timestamp;
    return timestamp;
  }

private:
  std::uint64_t source_generation_ = 0;
  std::uint32_t offset_ = 0;
  std::uint32_t last_timestamp_ = 0;
  bool started_ = false;
};

struct OfferedVideo
{
  std::string mid;
  uint8_t vp8_payload_type = 0;
};
std::vector<OfferedVideo> offered_videos(rtc::Description& offer)
{
  std::vector<OfferedVideo> videos;
  for (int index = 0; index < offer.mediaCount(); ++index)
  {
    auto entry = offer.media(index);
    if (!std::holds_alternative<rtc::Description::Media*>(entry))
      continue;
    auto* media = std::get<rtc::Description::Media*>(entry);
    if (media->type() != "video" || media->isRemoved() ||
        media->direction() == rtc::Description::Direction::Inactive ||
        media->direction() == rtc::Description::Direction::SendOnly)
      continue;
    for (int payload : media->payloadTypes())
    {
      auto* map = media->rtpMap(payload);
      if (!map)
        continue;
      auto format = map->format;
      std::transform(format.begin(),
                     format.end(),
                     format.begin(),
                     [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
      if (format == "VP8" && payload >= 0 && payload <= 127)
      {
        videos.push_back({media->mid(), static_cast<uint8_t>(payload)});
        break;
      }
    }
  }
  return videos;
}

struct OfferedAudio
{
  std::string mid;
  std::uint8_t opus_payload_type = 0;
  bool supports_quality_profile = false;
};

bool opus_parameter_is(std::span<const std::string> parameters,
                       std::string_view name,
                       std::string_view expected)
{
  const std::string needle = std::string(name) + "=";
  for (const auto& parameters_entry : parameters)
  {
    std::size_t start = 0;
    while (start < parameters_entry.size())
    {
      const auto end = parameters_entry.find(';', start);
      const auto parameter = std::string_view(parameters_entry).substr(start, end - start);
      if (parameter.starts_with(needle) && parameter.substr(needle.size()) == expected)
        return true;
      if (end == std::string::npos)
        break;
      start = end + 1;
    }
  }
  return false;
}

bool opus_bitrate_supported(std::span<const std::string> parameters)
{
  constexpr std::string_view prefix = "maxaveragebitrate=";
  for (const auto& parameters_entry : parameters)
  {
    const auto position = parameters_entry.find(prefix);
    if (position == std::string::npos)
      continue;
    const auto begin = position + prefix.size();
    const auto end = parameters_entry.find(';', begin);
    const auto value = parameters_entry.substr(begin, end - begin);
    try
    {
      return std::stoul(value) >= 128'000;
    }
    catch (...)
    {
      return false;
    }
  }
  return true;
}

std::vector<OfferedAudio> offered_audios(rtc::Description& offer)
{
  std::vector<OfferedAudio> result;
  for (int index = 0; index < offer.mediaCount(); ++index)
  {
    auto entry = offer.media(index);
    if (!std::holds_alternative<rtc::Description::Media*>(entry))
      continue;
    auto* media = std::get<rtc::Description::Media*>(entry);
    if (media->type() != "audio" || media->isRemoved() ||
        media->direction() == rtc::Description::Direction::Inactive ||
        media->direction() == rtc::Description::Direction::SendOnly)
      continue;
    for (int payload : media->payloadTypes())
    {
      auto* map = media->rtpMap(payload);
      if (!map)
        continue;
      auto format = map->format;
      std::transform(format.begin(),
                     format.end(),
                     format.begin(),
                     [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
      if (format == "OPUS" && map->clockRate == 48'000 && payload >= 0 && payload <= 127)
      {
        const bool stereo = opus_parameter_is(map->fmtps, "stereo", "1");
        result.push_back({media->mid(),
                          static_cast<std::uint8_t>(payload),
                          stereo && opus_bitrate_supported(map->fmtps)});
        break;
      }
    }
  }
  return result;
}

class RtcpKeyframeFeedbackHandler final : public rtc::MediaHandler
{
public:
  explicit RtcpKeyframeFeedbackHandler(std::function<void()> request_keyframe)
      : request_keyframe_(std::move(request_keyframe))
  {
  }

  void incoming(rtc::message_vector& messages, const rtc::message_callback&) override
  {
    for (const auto& message : messages)
    {
      if (!message || message->type != rtc::Message::Control)
        continue;
      std::size_t offset = 0;
      while (offset + sizeof(rtc::RtcpHeader) <= message->size())
      {
        const auto* header = reinterpret_cast<const rtc::RtcpHeader*>(message->data() + offset);
        const auto packet_size = header->lengthInBytes();
        if (packet_size < sizeof(rtc::RtcpHeader) || offset + packet_size > message->size())
          break;
        if (header->payloadType() == 206 &&
            (header->reportCount() == 1 || header->reportCount() == 4))
          request_keyframe_();
        offset += packet_size;
      }
    }
  }

private:
  std::function<void()> request_keyframe_;
};
} // namespace

VideoTrackPreferences normalize_video_preferences(VideoTrackPreferences preferences)
{
  preferences.target_fps = std::clamp(preferences.target_fps, 1U, 15U);
  preferences.max_bitrate_kbps = std::clamp(preferences.max_bitrate_kbps, 250U, 5000U);
  return preferences;
}

EncodedVideoDimensions encoded_video_dimensions(std::uint32_t width, std::uint32_t height)
{
  if (width > 960)
  {
    height = static_cast<std::uint32_t>(static_cast<std::uint64_t>(height) * 960 / width);
    width = 960;
  }
  if (height > 540)
  {
    width = static_cast<std::uint32_t>(static_cast<std::uint64_t>(width) * 540 / height);
    height = 540;
  }
  return {width & ~1U, height & ~1U};
}

void configure_audio_sender(
    const std::shared_ptr<rtc::Track>& track,
    std::uint32_t ssrc,
    std::uint8_t payload_type,
    std::shared_ptr<rtc::OpusRtpPacketizer>& packetizer,
    std::shared_ptr<AudioEgressObserver>& egress_observer,
    std::function<void(AudioRtcpObservation)> report,
    std::function<void(std::uint32_t)> nack,
    std::function<void(std::uint32_t, std::uint32_t, std::uint32_t)> retransmission)
{
  auto configuration = std::make_shared<rtc::RtpPacketizationConfig>(
      ssrc, "obs-mobile-companion", payload_type, rtc::OpusRtpPacketizer::DefaultClockRate);
  packetizer = std::make_shared<rtc::OpusRtpPacketizer>(configuration);
  egress_observer = std::make_shared<AudioEgressObserver>();
  packetizer->addToChain(egress_observer);
  packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(configuration));
  packetizer->addToChain(std::make_shared<AudioNackResponder>(std::move(retransmission)));
  packetizer->addToChain(
      std::make_shared<AudioRtcpFeedbackHandler>(ssrc, std::move(report), std::move(nack)));
  track->setMediaHandler(packetizer);
}

void configure_video_sender(const std::shared_ptr<rtc::Track>& track,
                            std::uint32_t ssrc,
                            std::uint8_t payload_type,
                            std::function<void()> request_keyframe,
                            std::shared_ptr<Vp8RtpPacketizer>& packetizer)
{
  auto configuration = std::make_shared<rtc::RtpPacketizationConfig>(
      ssrc, "obs-mobile-companion", payload_type, rtc::RtpPacketizer::VideoClockRate);
  packetizer = std::make_shared<Vp8RtpPacketizer>(configuration, 1150);
  packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(configuration));
  packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
  packetizer->addToChain(
      std::make_shared<RtcpKeyframeFeedbackHandler>(std::move(request_keyframe)));
  track->setMediaHandler(packetizer);
}

WebRtcHub::WebRtcHub(StateStore& state, RealtimeAudioServices* realtime_audio)
    : state_(state), realtime_audio_(realtime_audio)
{
}
WebRtcHub::~WebRtcHub()
{
  stop();
}

void WebRtcHub::assign_track_ssrcs(Peer& peer)
{
  std::lock_guard lock(ssrc_allocator_mutex_);
  std::array<std::uint32_t, 4> taken{};
  std::size_t count = 0;
  for (auto* ssrc :
       {&peer.program_ssrc, &peer.preview_ssrc, &peer.audio_monitor_ssrc, &peer.audio_master_ssrc})
  {
    *ssrc = ssrc_allocator_.allocate(std::span<const std::uint32_t>(taken.data(), count));
    taken[count++] = *ssrc;
  }
}
void WebRtcHub::start()
{
  if (rtc_lifetime_acquired_)
    return;
  libdatachannel_process_lifetime().acquire();
  rtc_lifetime_acquired_ = true;
  encoded_audio_frames_.clear();
  {
    std::lock_guard lock(mutex_);
    stopping_ = false;
  }
  try
  {
    program_thread_ = std::thread([this] { encode_loop("program"); });
    preview_thread_ = std::thread([this] { encode_loop("preview"); });
    audio_thread_ = std::thread([this] { audio_encode_loop(); });
    audio_sender_thread_ = std::thread([this] { audio_sender_loop(); });
    audio_master_thread_ = std::thread([this] { audio_master_encode_loop(); });
    audio_benchmark_thread_ = std::thread([this] { audio_complexity_benchmark(); });
    telemetry_thread_ = std::thread([this] { telemetry_loop(); });
  }
  catch (...)
  {
    stop();
    throw;
  }
  emit_diagnostic_event("hubStarted");
}
void WebRtcHub::stop()
{
  if (!rtc_lifetime_acquired_)
    return;
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    for (const auto& peer : pending_offers_)
      peer->cancel_offer(MediaOfferFailure::hub_stopped);
  }
  emit_diagnostic_event("hubStopping", health());
  frame_cv_.notify_all();
  {
    std::unique_lock lock(mutex_);
    pending_offers_finished_.wait(lock, [this] { return pending_offers_.empty(); });
  }
  if (program_thread_.joinable())
    program_thread_.join();
  if (preview_thread_.joinable())
    preview_thread_.join();
  if (audio_thread_.joinable())
    audio_thread_.join();
  if (audio_sender_thread_.joinable())
    audio_sender_thread_.join();
  if (audio_master_thread_.joinable())
    audio_master_thread_.join();
  if (audio_benchmark_thread_.joinable())
    audio_benchmark_thread_.join();
  if (telemetry_thread_.joinable())
    telemetry_thread_.join();
  encoded_audio_frames_.clear();
  std::unordered_map<std::string, std::shared_ptr<Peer>> removed_peers;
  {
    std::lock_guard lock(mutex_);
    if (!peers_.empty())
      ++video_demand_epoch_;
    removed_peers.swap(peers_);
  }
  for (const auto& [_, peer] : removed_peers)
    retire_peer(peer, "hubStopped");
  removed_peers.clear();
  libdatachannel_process_lifetime().release();
  rtc_lifetime_acquired_ = false;
  emit_diagnostic_event("hubStopped");
}

MediaOfferResult WebRtcHub::answer(std::string_view device_id,
                                   std::string_view offer_sdp,
                                   VideoOfferPreferences preferences,
                                   const std::function<bool()>& authorized)
{
  auto peer = std::make_shared<Peer>();
  peer->device_id = device_id;
  PendingOffer pending(*this, peer);
  if (const auto failure = pending.failure())
    return *failure;
  if (authorized && !authorized())
    return MediaOfferFailure::device_removed;
  preferences.program = normalize_video_preferences(preferences.program);
  preferences.preview = normalize_video_preferences(preferences.preview);
  emit_diagnostic_event("offerReceived",
                        {{"deviceId", device_id},
                         {"offerBytes", offer_sdp.size()},
                         {"programFps", preferences.program.target_fps},
                         {"previewFps", preferences.preview.target_fps},
                         {"audioMonitorRequested", preferences.audio_monitor},
                         {"audioMasterRequested", preferences.audio_master}});
  if (const auto failure = pending.failure())
    return *failure;
  rtc::Description offer(std::string(offer_sdp), "offer");
  auto videos = offered_videos(offer);
  const auto audios = offered_audios(offer);
  std::size_t audio_index = 0;
  if (videos.empty())
    throw std::invalid_argument("WebRTC offer has no receive-capable VP8 video m-line");
  assign_track_ssrcs(*peer);
  peer->audio_epoch = next_audio_epoch_.fetch_add(1, std::memory_order_relaxed);
  peer->media_session_id =
      "media-" + std::to_string(next_media_session_id_.fetch_add(1, std::memory_order_relaxed));
  peer->program_preferences = preferences.program;
  peer->preview_preferences = preferences.preview;
  emit_diagnostic_event("peerCreated",
                        {{"deviceId", peer->device_id},
                         {"mediaSessionId", peer->media_session_id},
                         {"audioEpoch", peer->audio_epoch},
                         {"programSsrc", peer->program_ssrc},
                         {"previewSsrc", peer->preview_ssrc},
                         {"audioMonitorSsrc", peer->audio_monitor_ssrc},
                         {"audioMasterSsrc", peer->audio_master_ssrc}});
  if (const auto failure = pending.failure())
    return *failure;
  rtc::Configuration config;
  config.disableAutoNegotiation = true;
  config.forceMediaTransport = true;
  peer->connection = std::make_shared<rtc::PeerConnection>(config);
  peer->connection->onLocalDescription([](rtc::Description) {});
  peer->connection->onGatheringStateChange(
      [this, weak = std::weak_ptr(peer)](rtc::PeerConnection::GatheringState state)
      {
        if (const auto current = weak.lock())
          emit_diagnostic_event("gatheringStateChanged",
                                {{"deviceId", current->device_id},
                                 {"mediaSessionId", current->media_session_id},
                                 {"audioEpoch", current->audio_epoch},
                                 {"state", gathering_state_name(state)}});
        if (state != rtc::PeerConnection::GatheringState::Complete)
          return;
        if (auto p = weak.lock())
        {
          auto description = p->connection->localDescription();
          if (!description)
            return;
          std::lock_guard lock(p->description_mutex);
          p->local_sdp = std::string(*description);
          p->description_cv.notify_all();
        }
      });
  peer->connection->onStateChange(
      [this, id = peer->device_id, weak = std::weak_ptr(peer)](rtc::PeerConnection::State state)
      {
        if (const auto current = weak.lock())
          emit_diagnostic_event("peerStateChanged",
                                {{"deviceId", current->device_id},
                                 {"mediaSessionId", current->media_session_id},
                                 {"audioEpoch", current->audio_epoch},
                                 {"state", peer_state_name(state)}});
        if (state != rtc::PeerConnection::State::Disconnected &&
            state != rtc::PeerConnection::State::Failed &&
            state != rtc::PeerConnection::State::Closed)
          return;
        if (auto expected = weak.lock())
          remove_peer(id, expected);
      });
  peer->program_payload_type = videos[0].vp8_payload_type;
  rtc::Description::Video program(videos[0].mid, rtc::Description::Direction::SendOnly);
  program.addVP8Codec(peer->program_payload_type);
  program.addSSRC(peer->program_ssrc, "program", "obs-mobile-companion", "program");
  peer->program = peer->connection->addTrack(program);
  configure_video_sender(
      peer->program,
      peer->program_ssrc,
      peer->program_payload_type,
      [this, weak = std::weak_ptr(peer)]
      {
        request_keyframe("program");
        if (const auto current = weak.lock())
        {
          const auto count = current->program_rtcp_feedback.fetch_add(1) + 1;
          if (count == 1)
            emit_diagnostic_event("firstVideoRtcpFeedback",
                                  {{"deviceId", current->device_id},
                                   {"mediaSessionId", current->media_session_id},
                                   {"track", "program"}});
        }
      },
      peer->program_packetizer);
  emit_diagnostic_event("trackBound",
                        {{"deviceId", peer->device_id},
                         {"mediaSessionId", peer->media_session_id},
                         {"track", "program"},
                         {"payloadType", peer->program_payload_type}});
  peer->program->onOpen(
      [this, weak = std::weak_ptr(peer)]
      {
        request_keyframe("program");
        if (const auto current = weak.lock())
          emit_diagnostic_event("trackOpened",
                                {{"deviceId", current->device_id},
                                 {"mediaSessionId", current->media_session_id},
                                 {"audioEpoch", current->audio_epoch},
                                 {"track", "program"}});
      });
  if (state_.studio_mode() && videos.size() > 1)
  {
    peer->preview_payload_type = videos[1].vp8_payload_type;
    rtc::Description::Video preview(videos[1].mid, rtc::Description::Direction::SendOnly);
    preview.addVP8Codec(peer->preview_payload_type);
    preview.addSSRC(peer->preview_ssrc, "preview", "obs-mobile-companion", "preview");
    peer->preview = peer->connection->addTrack(preview);
    configure_video_sender(
        peer->preview,
        peer->preview_ssrc,
        peer->preview_payload_type,
        [this, weak = std::weak_ptr(peer)]
        {
          request_keyframe("preview");
          if (const auto current = weak.lock())
          {
            const auto count = current->preview_rtcp_feedback.fetch_add(1) + 1;
            if (count == 1)
              emit_diagnostic_event("firstVideoRtcpFeedback",
                                    {{"deviceId", current->device_id},
                                     {"mediaSessionId", current->media_session_id},
                                     {"track", "preview"}});
          }
        },
        peer->preview_packetizer);
    emit_diagnostic_event("trackBound",
                          {{"deviceId", peer->device_id},
                           {"mediaSessionId", peer->media_session_id},
                           {"track", "preview"},
                           {"payloadType", peer->preview_payload_type}});
  }
  if (peer->preview)
  {
    peer->preview->onOpen(
        [this, weak = std::weak_ptr(peer)]
        {
          request_keyframe("preview");
          if (const auto current = weak.lock())
            emit_diagnostic_event("trackOpened",
                                  {{"deviceId", current->device_id},
                                   {"mediaSessionId", current->media_session_id},
                                   {"audioEpoch", current->audio_epoch},
                                   {"track", "preview"}});
        });
  }
  std::string audio_unavailable_reason;
  if (preferences.audio_monitor)
  {
    const auto* audio = audio_index < audios.size() ? &audios[audio_index++] : nullptr;
    const auto monitor_status = audio_monitor_status();
    if (!monitor_status.available)
      audio_unavailable_reason =
          monitor_status.reason.empty() ? "backend-unavailable" : monitor_status.reason;
    else if (!audio || !audio->supports_quality_profile)
      audio_unavailable_reason = "media-negotiating";
    else
    {
      rtc::Description::Audio monitor(audio->mid, rtc::Description::Direction::SendOnly);
      monitor.addOpusCodec(audio->opus_payload_type,
                           "minptime=10;maxaveragebitrate=128000;stereo=1;sprop-stereo=1;"
                           "useinbandfec=0;usedtx=0");
      monitor.addAttribute("ptime:20");
      monitor.addAttribute("maxptime:20");
      monitor.addSSRC(
          peer->audio_monitor_ssrc, "audio-monitor", "obs-mobile-companion", "audio-monitor");
      peer->audio_monitor = peer->connection->addTrack(monitor);
      configure_audio_sender(
          peer->audio_monitor,
          peer->audio_monitor_ssrc,
          audio->opus_payload_type,
          peer->audio_monitor_packetizer,
          peer->audio_egress_observer,
          [weak = std::weak_ptr(peer)](AudioRtcpObservation report)
          {
            if (const auto current = weak.lock())
            {
              current->audio_rtcp_reports.fetch_add(1, std::memory_order_relaxed);
              current->audio_fraction_lost.store(report.fraction_lost, std::memory_order_relaxed);
              current->audio_packets_lost.store(report.packets_lost, std::memory_order_relaxed);
              current->audio_jitter.store(report.jitter, std::memory_order_relaxed);
              current->audio_round_trip_ms.store(report.round_trip_ms, std::memory_order_relaxed);
              current->audio_extended_highest_sequence.store(report.extended_highest_sequence,
                                                             std::memory_order_relaxed);
            }
          },
          [weak = std::weak_ptr(peer)](std::uint32_t requested_packets)
          {
            if (const auto current = weak.lock())
              current->audio_nacks_received.fetch_add(requested_packets, std::memory_order_relaxed);
          },
          [weak = std::weak_ptr(peer)](
              std::uint32_t retransmitted, std::uint32_t deadline_missed, std::uint32_t send_failed)
          {
            if (const auto current = weak.lock())
            {
              current->audio_nacks_retransmitted.fetch_add(retransmitted,
                                                           std::memory_order_relaxed);
              current->audio_nack_deadline_misses.fetch_add(deadline_missed,
                                                            std::memory_order_relaxed);
              current->audio_nack_send_failures.fetch_add(send_failed, std::memory_order_relaxed);
            }
          });
      peer->audio_monitor->onOpen(
          [this, weak = std::weak_ptr(peer)]
          {
            if (const auto current = weak.lock())
              emit_diagnostic_event(
                  "trackOpened",
                  {{"audioEpoch", current->audio_epoch}, {"track", "audio-monitor"}});
          });
    }
  }
  std::string audio_master_unavailable_reason;
  if (preferences.audio_master)
  {
    const auto* audio = audio_index < audios.size() ? &audios[audio_index++] : nullptr;
    const auto master_status = audio_master_status();
    if (!master_status.available)
      audio_master_unavailable_reason =
          master_status.reason.empty() ? "backend-unavailable" : master_status.reason;
    else if (!audio || !audio->supports_quality_profile)
      audio_master_unavailable_reason = "media-negotiating";
    else
    {
      rtc::Description::Audio master(audio->mid, rtc::Description::Direction::SendOnly);
      master.addOpusCodec(audio->opus_payload_type,
                          "minptime=10;maxaveragebitrate=128000;stereo=1;sprop-stereo=1;"
                          "useinbandfec=0;usedtx=0");
      master.addAttribute("ptime:20");
      master.addAttribute("maxptime:20");
      master.addSSRC(
          peer->audio_master_ssrc, "audio-master", "obs-mobile-companion", "audio-master");
      peer->audio_master = peer->connection->addTrack(master);
      std::shared_ptr<AudioEgressObserver> ignored_egress;
      configure_audio_sender(
          peer->audio_master,
          peer->audio_master_ssrc,
          audio->opus_payload_type,
          peer->audio_master_packetizer,
          ignored_egress,
          [](AudioRtcpObservation) {},
          [](std::uint32_t) {},
          [](std::uint32_t, std::uint32_t, std::uint32_t) {});
      peer->audio_master->onOpen(
          [this, weak = std::weak_ptr(peer)]
          {
            if (const auto current = weak.lock())
              emit_diagnostic_event(
                  "trackOpened", {{"audioEpoch", current->audio_epoch}, {"track", "audio-master"}});
          });
    }
  }
  rtc::DataChannelInit init;
  init.reliability.unordered = true;
  init.reliability.maxRetransmits = 0;
  peer->telemetry = peer->connection->createDataChannel("telemetry", init);
  peer->telemetry->onOpen(
      [this, weak = std::weak_ptr(peer)]
      {
        if (const auto current = weak.lock())
          emit_diagnostic_event("dataChannelOpened",
                                {{"audioEpoch", current->audio_epoch}, {"channel", "telemetry"}});
      });
  peer->telemetry->onMessage(
      [this, weak = std::weak_ptr(peer)](std::variant<rtc::binary, rtc::string> message)
      {
        if (auto p = weak.lock())
          if (auto* text = std::get_if<rtc::string>(&message))
            try
            {
              auto value = json::parse(*text);
              if (value.value("type", "") == "mixerVisibility")
                p->mixer_open = value.value("open", false);
              else if (value.value("type", "") == "audioMonitorPlayback" && value.is_object())
              {
                const auto bounded = [&value](const char* name,
                                              std::uint64_t maximum) -> std::optional<std::uint64_t>
                {
                  const auto found = value.find(name);
                  if (found == value.end() || !found->is_number_unsigned())
                    return std::nullopt;
                  return std::min(found->get<std::uint64_t>(), maximum);
                };
                const auto plc_frames = bounded("plcFrames", 1'000'000'000ULL);
                const auto underruns = bounded("audioTrackUnderruns", 1'000'000'000ULL);
                const auto output_frames = bounded("outputFrames", 10'000'000'000ULL);
                const auto jitter_delay = bounded("jitterBufferDelayMs", 10'000ULL);
                if (!plc_frames || !underruns || !output_frames || !jitter_delay)
                  return;
                p->android_plc_frames.store(*plc_frames, std::memory_order_relaxed);
                p->android_audio_track_underruns.store(*underruns, std::memory_order_relaxed);
                p->android_output_frames.store(*output_frames, std::memory_order_relaxed);
                p->android_jitter_buffer_delay_ms.store(static_cast<std::uint32_t>(*jitter_delay),
                                                        std::memory_order_relaxed);
                p->android_audio_reports.fetch_add(1, std::memory_order_relaxed);
                emit_diagnostic_event("androidAudioPlaybackReport",
                                      {{"audioEpoch", p->audio_epoch},
                                       {"plcFrames", *plc_frames},
                                       {"audioTrackUnderruns", *underruns},
                                       {"outputFrames", *output_frames},
                                       {"jitterBufferDelayMs", *jitter_delay}});
              }
            }
            catch (...)
            {
            }
      });
  if (const auto failure = pending.failure())
    return *failure;
  peer->connection->setRemoteDescription(std::move(offer));
  peer->connection->setLocalDescription();
  {
    std::unique_lock lock(peer->description_mutex);
    if (!peer->description_cv.wait_for(
            lock,
            std::chrono::seconds(8),
            [&] { return peer->local_sdp.has_value() || peer->offer_failure.has_value(); }))
    {
      lock.unlock();
      emit_diagnostic_event("negotiationTimedOut",
                            {{"deviceId", peer->device_id},
                             {"mediaSessionId", peer->media_session_id},
                             {"audioEpoch", peer->audio_epoch}});
      throw std::runtime_error("WebRTC negotiation timed out");
    }
    if (peer->offer_failure)
      return *peer->offer_failure;
  }
  if (peer->audio_monitor)
  {
    peer->audio_sender_thread =
        std::thread([this, raw_peer = peer.get()] { audio_peer_sender_loop(*raw_peer); });
    peer->audio_sender_id = peer->audio_sender_thread.get_id();
  }
  if (peer->audio_master)
  {
    peer->audio_master_sender_thread =
        std::thread([this, raw_peer = peer.get()] { audio_master_peer_sender_loop(*raw_peer); });
    peer->audio_master_sender_id = peer->audio_master_sender_thread.get_id();
  }
  json response = {
      {"type", "answer"},
      {"sdp", *peer->local_sdp},
      {"mediaSessionId", peer->media_session_id},
      {"program", true},
      {"preview", peer->preview != nullptr},
      {"audioMonitor", peer->audio_monitor != nullptr},
      {"audioMaster", peer->audio_master != nullptr},
      {"applied",
       {{"program",
         {{"targetFps", peer->program_preferences.target_fps},
          {"maxBitrateKbps", peer->program_preferences.max_bitrate_kbps}}},
        {"preview",
         peer->preview ? json{{"targetFps", peer->preview_preferences.target_fps},
                              {"maxBitrateKbps", peer->preview_preferences.max_bitrate_kbps}}
                       : json(nullptr)}}}};
  if (preferences.audio_monitor && !peer->audio_monitor)
    response["audioMonitorUnavailableReason"] = std::move(audio_unavailable_reason);
  if (preferences.audio_master && !peer->audio_master)
    response["audioMasterUnavailableReason"] = std::move(audio_master_unavailable_reason);
  emit_diagnostic_event(
      "peerPrepared", {{"deviceId", peer->device_id}, {"mediaSessionId", peer->media_session_id}});
  if (const auto failure = pending.publish())
    return *failure;
  request_keyframe("program");
  if (peer->preview)
    request_keyframe("preview");
  emit_diagnostic_event("answerCreated",
                        {{"deviceId", peer->device_id},
                         {"mediaSessionId", peer->media_session_id},
                         {"audioEpoch", peer->audio_epoch},
                         {"program", true},
                         {"preview", peer->preview != nullptr},
                         {"audioMonitor", peer->audio_monitor != nullptr},
                         {"audioMaster", peer->audio_master != nullptr}});
  return response;
}
void WebRtcHub::remove_device(std::string_view id)
{
  std::shared_ptr<Peer> removed;
  {
    std::lock_guard lock(mutex_);
    for (const auto& peer : pending_offers_)
      if (peer->device_id == id)
        peer->cancel_offer(MediaOfferFailure::device_removed);
    auto found = peers_.find(std::string(id));
    if (found != peers_.end())
    {
      removed = std::move(found->second);
      peers_.erase(found);
      ++video_demand_epoch_;
    }
  }
  audio_recovery_requested_.store(true, std::memory_order_release);
  if (removed)
    retire_peer(removed, "deviceRemoved");
}

MediaSessionRetirementResult WebRtcHub::retire_device_session(std::string_view id,
                                                              std::string_view media_session_id)
{
  std::shared_ptr<Peer> removed;
  {
    std::lock_guard lock(mutex_);
    const auto found = peers_.find(std::string(id));
    if (found == peers_.end())
      return {MediaSessionRetirementStatus::no_active_session, {}, 0};
    if (found->second->media_session_id != media_session_id)
      return {MediaSessionRetirementStatus::session_mismatch, found->second->media_session_id, 0};
    removed = std::move(found->second);
    peers_.erase(found);
    ++video_demand_epoch_;
  }
  audio_recovery_requested_.store(true, std::memory_order_release);
  const auto failures = removed->retire();
  emit_diagnostic_event("peerRetired",
                        {{"deviceId", removed->device_id},
                         {"mediaSessionId", removed->media_session_id},
                         {"reason", "clientRecovery"},
                         {"cleanupFailures", failures}});
  return {failures == 0 ? MediaSessionRetirementStatus::retired
                        : MediaSessionRetirementStatus::failed,
          removed->media_session_id,
          failures};
}

void WebRtcHub::retire_peer(const std::shared_ptr<Peer>& peer, std::string_view reason)
{
  const auto failures = peer->retire();
  emit_diagnostic_event("peerRetired",
                        {{"deviceId", peer->device_id},
                         {"mediaSessionId", peer->media_session_id},
                         {"reason", reason},
                         {"cleanupFailures", failures}});
}
void WebRtcHub::remove_peer(std::string_view id, const std::shared_ptr<Peer>& expected)
{
  std::shared_ptr<Peer> removed;
  {
    std::lock_guard lock(mutex_);
    auto found = peers_.find(std::string(id));
    if (found != peers_.end() && found->second == expected)
    {
      const auto current_thread = std::this_thread::get_id();
      if (expected->audio_sender_id == current_thread ||
          expected->audio_master_sender_id == current_thread)
      {
        expected->audio_stopping.store(true, std::memory_order_release);
        expected->audio_master_stopping.store(true, std::memory_order_release);
        expected->removal_requested.store(true, std::memory_order_release);
        expected->audio_cv.notify_all();
        expected->audio_master_cv.notify_all();
        return;
      }
      removed = std::move(found->second);
      peers_.erase(found);
      ++video_demand_epoch_;
    }
    else if (pending_offers_.contains(expected))
      expected->cancel_offer(MediaOfferFailure::transport_closed);
  }
  if (removed)
  {
    audio_recovery_requested_.store(true, std::memory_order_release);
    retire_peer(removed, "transportState");
  }
}

void WebRtcHub::on_diagnostic_event(DiagnosticEventHandler handler)
{
  std::lock_guard lock(diagnostic_event_mutex_);
  diagnostic_event_handler_ = std::move(handler);
}

void WebRtcHub::emit_diagnostic_event(std::string_view event, json details) const
{
  DiagnosticEventHandler handler;
  {
    std::lock_guard lock(diagnostic_event_mutex_);
    handler = diagnostic_event_handler_;
  }
  if (handler)
    handler(event, std::move(details));
}
void WebRtcHub::request_keyframe(std::string_view track)
{
  if (track == "program")
    program_keyframe_requests_.fetch_add(1, std::memory_order_release);
  else
    preview_keyframe_requests_.fetch_add(1, std::memory_order_release);
}
bool WebRtcHub::has_open_track(std::string_view track) const
{
  std::lock_guard lock(mutex_);
  for (const auto& [_, peer] : peers_)
  {
    auto media = track == "program" ? peer->program : peer->preview;
    if (media && media->isOpen())
      return true;
  }
  return false;
}

std::vector<VideoTrackPreferences> WebRtcHub::active_policies(std::string_view track) const
{
  std::lock_guard lock(mutex_);
  std::vector<VideoTrackPreferences> policies;
  for (const auto& [_, peer] : peers_)
  {
    const auto media = track == "program" ? peer->program : peer->preview;
    if (!media || !media->isOpen())
      continue;
    const auto policy = track == "program" ? peer->program_preferences : peer->preview_preferences;
    if (std::find(policies.begin(), policies.end(), policy) == policies.end())
      policies.push_back(policy);
  }
  return policies;
}

std::uint32_t WebRtcHub::capture_target_fps(std::string_view track) const
{
  std::lock_guard lock(mutex_);
  std::uint32_t target = 1;
  bool found = false;
  for (const auto& [_, peer] : peers_)
  {
    const auto media = track == "program" ? peer->program : peer->preview;
    if (!media)
      continue;
    found = true;
    const auto& preferences =
        track == "program" ? peer->program_preferences : peer->preview_preferences;
    target = std::max(target, preferences.target_fps);
  }
  return found ? target : 1;
}

VideoDemand WebRtcHub::video_demand() const
{
  std::lock_guard lock(mutex_);
  VideoDemand demand;
  demand.epoch = video_demand_epoch_;
  demand.active_video_client_count = static_cast<std::uint32_t>(peers_.size());
  for (const auto& [_, peer] : peers_)
  {
    if (peer->program)
      demand.requested_program_fps =
          std::max(demand.requested_program_fps, peer->program_preferences.target_fps);
    if (peer->preview)
      demand.requested_preview_fps =
          std::max(demand.requested_preview_fps, peer->preview_preferences.target_fps);
  }
  return demand;
}
void WebRtcHub::push_frame(std::string_view track, std::shared_ptr<const RawVideoFrame> frame)
{
  {
    std::lock_guard lock(mutex_);
    latest_frames_[std::string(track)] = std::move(frame);
    ++frame_versions_[std::string(track)];
  }
  frame_cv_.notify_all();
}

void WebRtcHub::restart_video_source() noexcept
{
  {
    std::lock_guard lock(mutex_);
    ++program_source_generation_;
    ++preview_source_generation_;
    for (auto frame = latest_frames_.begin(); frame != latest_frames_.end();)
    {
      if (frame->first == "program" || frame->first == "preview")
        frame = latest_frames_.erase(frame);
      else
        ++frame;
    }
    for (auto& [track, encoded_version] : encoded_versions_)
    {
      if (track != "program" && track != "preview")
        continue;
      const auto frame_version = frame_versions_.find(track);
      if (frame_version != frame_versions_.end())
        encoded_version = frame_version->second;
    }
  }
  request_keyframe("program");
  request_keyframe("preview");
  frame_cv_.notify_all();
}

void WebRtcHub::restart_video_sessions() noexcept
{
  std::unordered_map<std::string, std::shared_ptr<Peer>> retired_peers;
  {
    std::lock_guard lock(mutex_);
    for (const auto& peer : pending_offers_)
      peer->cancel_offer(MediaOfferFailure::capture_reset);
    ++program_source_generation_;
    ++preview_source_generation_;
    for (auto frame = latest_frames_.begin(); frame != latest_frames_.end();)
    {
      if (frame->first == "program" || frame->first == "preview")
        frame = latest_frames_.erase(frame);
      else
        ++frame;
    }
    for (auto& [track, encoded_version] : encoded_versions_)
    {
      if (track != "program" && track != "preview")
        continue;
      const auto frame_version = frame_versions_.find(track);
      if (frame_version != frame_versions_.end())
        encoded_version = frame_version->second;
    }
    if (!peers_.empty())
      ++video_demand_epoch_;
    retired_peers.swap(peers_);
  }

  audio_recovery_requested_.store(true, std::memory_order_release);
  frame_cv_.notify_all();
  for (const auto& [_, peer] : retired_peers)
    retire_peer(peer, "videoSourceRestart");
}

void WebRtcHub::push_audio_monitor(SharedAudioMonitorFrame frame)
{
  if (!frame)
    return;
  bool requested = false;
  {
    std::lock_guard lock(mutex_);
    requested =
        post_opus_diagnostic_enabled_.load(std::memory_order_relaxed) ||
        std::any_of(peers_.begin(),
                    peers_.end(),
                    [](const auto& entry) { return entry.second->audio_monitor != nullptr; });
    if (!requested)
    {
      audio_frames_.clear();
      return;
    }
  }
  if (frame->sample_rate == ipc::audio_monitor_sample_rate && frame->channels == 2 &&
      frame->samples.size() ==
          static_cast<std::size_t>(ipc::audio_monitor_frame_samples) * frame->channels)
    audio_level_telemetry_.observe(*frame);
  {
    std::lock_guard lock(mutex_);
    audio_queue_drops_.fetch_add(audio_frames_.push(std::move(frame)), std::memory_order_relaxed);
  }
  frame_cv_.notify_all();
}

void WebRtcHub::push_audio_master(SharedAudioMonitorFrame frame)
{
  if (!frame)
    return;
  if (frame->sample_rate == ipc::audio_monitor_sample_rate && frame->channels == 2 &&
      frame->samples.size() ==
          static_cast<std::size_t>(ipc::audio_monitor_frame_samples) * frame->channels)
    audio_master_level_telemetry_.observe(*frame);
  {
    std::lock_guard lock(mutex_);
    const bool requested =
        std::any_of(peers_.begin(),
                    peers_.end(),
                    [](const auto& entry) { return entry.second->audio_master != nullptr; });
    if (!requested)
    {
      audio_master_frames_.clear();
      return;
    }
    audio_master_queue_drops_.fetch_add(audio_master_frames_.push(std::move(frame)),
                                        std::memory_order_relaxed);
  }
  frame_cv_.notify_all();
}

void WebRtcHub::observe_source_frame(std::string_view track)
{
  std::vector<std::shared_ptr<Peer>> first_observers;
  {
    std::lock_guard lock(mutex_);
    for (const auto& [_, peer] : peers_)
    {
      const auto& media = track == "program" ? peer->program : peer->preview;
      if (!media || !media->isOpen() || peer->retiring.load(std::memory_order_acquire))
        continue;
      auto& frames = track == "program" ? peer->program_source_frames : peer->preview_source_frames;
      if (frames.fetch_add(1, std::memory_order_relaxed) == 0)
        first_observers.push_back(peer);
    }
  }
  for (const auto& peer : first_observers)
    emit_diagnostic_event("firstVideoSourceFrame",
                          {{"deviceId", peer->device_id},
                           {"mediaSessionId", peer->media_session_id},
                           {"track", track}});
}

void WebRtcHub::observe_encoder_input(std::string_view track, const VideoTrackPreferences& policy)
{
  std::vector<std::shared_ptr<Peer>> first_observers;
  {
    std::lock_guard lock(mutex_);
    for (const auto& [_, peer] : peers_)
    {
      const auto& media = track == "program" ? peer->program : peer->preview;
      const auto& peer_policy =
          track == "program" ? peer->program_preferences : peer->preview_preferences;
      if (!media || !media->isOpen() || peer_policy != policy ||
          peer->retiring.load(std::memory_order_acquire))
        continue;
      auto& inputs =
          track == "program" ? peer->program_encoder_inputs : peer->preview_encoder_inputs;
      if (inputs.fetch_add(1, std::memory_order_relaxed) == 0)
        first_observers.push_back(peer);
    }
  }
  for (const auto& peer : first_observers)
    emit_diagnostic_event("firstVideoEncoderInput",
                          {{"deviceId", peer->device_id},
                           {"mediaSessionId", peer->media_session_id},
                           {"track", track}});
}

void WebRtcHub::record_encoder_failure(std::string_view track,
                                       const VideoTrackPreferences& policy,
                                       std::string_view stage,
                                       std::string_view reason)
{
  auto& failures = track == "program" ? program_encoder_failures_ : preview_encoder_failures_;
  const auto count = failures.fetch_add(1, std::memory_order_relaxed) + 1;
  if (count == 1 || (count & (count - 1)) == 0)
    emit_diagnostic_event("videoEncoderFailed",
                          {{"track", track},
                           {"stage", stage},
                           {"reason", std::string(reason).substr(0, 160)},
                           {"targetFps", policy.target_fps},
                           {"maxBitrateKbps", policy.max_bitrate_kbps},
                           {"failureCount", count}});
}

void WebRtcHub::set_post_opus_diagnostic_handler(
    std::function<void(SharedAudioMonitorFrame)> handler)
{
  std::lock_guard lock(mutex_);
  post_opus_diagnostic_handler_ = std::move(handler);
}

void WebRtcHub::set_post_opus_diagnostic_enabled(bool enabled) noexcept
{
  post_opus_diagnostic_enabled_.store(enabled, std::memory_order_release);
  frame_cv_.notify_all();
}

void WebRtcHub::set_audio_monitor_status(AudioMonitorStatus status)
{
  std::lock_guard lock(mutex_);
  if (audio_monitor_status_.available && !status.available)
    audio_recovery_requested_.store(true, std::memory_order_release);
  audio_monitor_status_ = std::move(status);
}

AudioMonitorStatus WebRtcHub::audio_monitor_status() const
{
  std::lock_guard lock(mutex_);
  auto status = audio_monitor_status_;
  if (audio_encoder_failed_)
  {
    status.available = false;
    status.reason = "backend-unavailable";
  }
  return status;
}

void WebRtcHub::set_audio_master_status(AudioMonitorStatus status)
{
  std::lock_guard lock(mutex_);
  audio_master_status_ = std::move(status);
}

AudioMonitorStatus WebRtcHub::audio_master_status() const
{
  std::lock_guard lock(mutex_);
  auto status = audio_master_status_;
  if (audio_master_encoder_failed_)
  {
    status.available = false;
    status.reason = "backend-unavailable";
  }
  return status;
}

void WebRtcHub::encode_loop(std::string track)
{
  auto& retained = track == "program" ? retained_program_policies_ : retained_preview_policies_;
  std::unordered_map<std::uint64_t, std::unique_ptr<Encoder>> policy_encoders;
  std::unordered_map<std::uint64_t, std::uint64_t> policy_keyframe_generations;
  std::unordered_map<std::uint64_t, VideoRtpTimeline> policy_rtp_timelines;
  std::uint64_t observed_source_generation = 0;
  while (!stopping_)
  {
    std::shared_ptr<const RawVideoFrame> frame;
    uint64_t version = 0;
    std::uint64_t source_generation = 0;
    {
      std::unique_lock lock(mutex_);
      frame_cv_.wait_for(
          lock,
          std::chrono::milliseconds(100),
          [&] { return stopping_ || frame_versions_[track] > encoded_versions_[track]; });
      if (stopping_)
        break;
      version = frame_versions_[track];
      frame = latest_frames_[track];
      encoded_versions_[track] = version;
      source_generation =
          track == "program" ? program_source_generation_ : preview_source_generation_;
    }
    const auto policies = active_policies(track);
    const auto obsolete = [&policies](const auto& entry)
    {
      return std::ranges::none_of(policies,
                                  [&](const auto& policy)
                                  {
                                    return entry.first ==
                                           ((static_cast<std::uint64_t>(policy.target_fps) << 32U) |
                                            policy.max_bitrate_kbps);
                                  });
    };
    // Release codec buffers and worker threads even when capture has stopped or the last peer left.
    std::erase_if(policy_encoders, obsolete);
    std::erase_if(policy_keyframe_generations, obsolete);
    std::erase_if(policy_rtp_timelines, obsolete);
    retained.store(policy_encoders.size(), std::memory_order_relaxed);
    if (!frame || !has_open_track(track))
      continue;
    if (source_generation != observed_source_generation)
    {
      policy_encoders.clear();
      policy_keyframe_generations.clear();
      observed_source_generation = source_generation;
    }
    observe_source_frame(track);
    const auto dimensions = encoded_video_dimensions(frame->width, frame->height);
    if (!dimensions.width || !dimensions.height)
      continue;
    auto& keyframe_requests =
        track == "program" ? program_keyframe_requests_ : preview_keyframe_requests_;
    const auto requested_keyframe = keyframe_requests.load(std::memory_order_acquire);
    for (const auto& policy : policies)
    {
      const auto policy_key =
          (static_cast<std::uint64_t>(policy.target_fps) << 32U) | policy.max_bitrate_kbps;
      auto& encoder = policy_encoders[policy_key];
      if (!encoder)
        encoder = std::make_unique<Encoder>();
      if (!encoder->cadence.should_publish(frame->captured_ns, policy.target_fps))
        continue;
      observe_encoder_input(track, policy);
      const auto duration = 90'000U / policy.target_fps;
      const auto rtp_timestamp =
          policy_rtp_timelines[policy_key].map(frame->captured_ns, source_generation, duration);
      encode_video_frame(track,
                         policy,
                         *frame,
                         dimensions,
                         rtp_timestamp,
                         requested_keyframe,
                         policy_keyframe_generations[policy_key],
                         encoder);
    }
    retained.store(policy_encoders.size(), std::memory_order_relaxed);
  }
  retained.store(0, std::memory_order_relaxed);
}

void WebRtcHub::encode_video_frame(std::string_view track,
                                   const VideoTrackPreferences& policy,
                                   const RawVideoFrame& frame,
                                   EncodedVideoDimensions dimensions,
                                   std::uint32_t rtp_timestamp,
                                   std::uint64_t requested_keyframe,
                                   std::uint64_t& completed_keyframe,
                                   std::unique_ptr<Encoder>& encoder)
{
  const bool force = requested_keyframe > completed_keyframe;
  try
  {
    encoder->configure(
        dimensions.width, dimensions.height, policy.target_fps, policy.max_bitrate_kbps);
    encoder->convert(frame);
    const auto duration = 90'000U / policy.target_fps;
    const auto result = vpx_codec_encode(&encoder->codec,
                                         &encoder->image,
                                         rtp_timestamp,
                                         duration,
                                         force ? VPX_EFLAG_FORCE_KF : 0,
                                         VPX_DL_REALTIME);
    if (result != VPX_CODEC_OK)
    {
      record_encoder_failure(track, policy, "encode", vpx_codec_err_to_string(result));
      encoder.reset();
      return;
    }
    vpx_codec_iter_t iterator = nullptr;
    bool produced_frame = false;
    while (const vpx_codec_cx_pkt_t* packet = vpx_codec_get_cx_data(&encoder->codec, &iterator))
      if (packet->kind == VPX_CODEC_CX_FRAME_PKT)
      {
        produced_frame = true;
        const bool keyframe = (packet->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
        broadcast_frame(
            track,
            policy,
            std::span(static_cast<const uint8_t*>(packet->data.frame.buf), packet->data.frame.sz),
            rtp_timestamp,
            keyframe);
        if (force && keyframe)
          completed_keyframe = requested_keyframe;
      }
    if (!produced_frame)
    {
      record_encoder_failure(track, policy, "output", "VP8 encoder produced no frame");
      encoder.reset();
    }
  }
  catch (const std::exception& error)
  {
    record_encoder_failure(track, policy, "exception", error.what());
    encoder.reset();
  }
  catch (...)
  {
    record_encoder_failure(track, policy, "exception", "unknown VP8 encoder failure");
    encoder.reset();
  }
}

void WebRtcHub::broadcast_frame(std::string_view track,
                                const VideoTrackPreferences& policy,
                                std::span<const uint8_t> encoded,
                                uint32_t timestamp,
                                bool)
{
  std::vector<std::shared_ptr<Peer>> peers;
  {
    std::lock_guard lock(mutex_);
    for (auto& [_, p] : peers_)
      peers.push_back(p);
  }
  for (auto& p : peers)
  {
    auto media = track == "program" ? p->program : p->preview;
    if (!media || !media->isOpen() || p->retiring.load(std::memory_order_acquire))
      continue;
    const auto& peer_policy = track == "program" ? p->program_preferences : p->preview_preferences;
    if (peer_policy != policy)
      continue;
    auto& attempts = track == "program" ? p->program_send_attempts : p->preview_send_attempts;
    auto& failures = track == "program" ? p->program_send_failures : p->preview_send_failures;
    const auto attempt = attempts.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto packets_before = media == p->program ? p->program_packetizer->packet_count()
                                                    : p->preview_packetizer->packet_count();
    try
    {
      media->sendFrame(reinterpret_cast<const std::byte*>(encoded.data()),
                       encoded.size(),
                       rtc::FrameInfo(timestamp));
      auto& frames = track == "program" ? p->program_frames : p->preview_frames;
      auto& last_timestamp =
          track == "program" ? p->program_last_timestamp : p->preview_last_timestamp;
      const auto encoded_frames = frames.fetch_add(1, std::memory_order_relaxed) + 1;
      last_timestamp.store(timestamp, std::memory_order_relaxed);
      if (encoded_frames == 1)
        emit_diagnostic_event("firstVideoEncodedFrame",
                              {{"deviceId", p->device_id},
                               {"mediaSessionId", p->media_session_id},
                               {"track", track}});
      const auto packets_after = media == p->program ? p->program_packetizer->packet_count()
                                                     : p->preview_packetizer->packet_count();
      if (packets_before == 0 && packets_after > 0)
        emit_diagnostic_event("firstVideoRtpPacketized",
                              {{"deviceId", p->device_id},
                               {"mediaSessionId", p->media_session_id},
                               {"track", track},
                               {"packetCount", packets_after}});
    }
    catch (const std::exception& error)
    {
      const auto failure = failures.fetch_add(1, std::memory_order_relaxed) + 1;
      p->drops.fetch_add(1, std::memory_order_relaxed);
      if (failure == 1)
        emit_diagnostic_event("videoSenderFailed",
                              {{"deviceId", p->device_id},
                               {"mediaSessionId", p->media_session_id},
                               {"track", track},
                               {"attempt", attempt},
                               {"reason", std::string(error.what()).substr(0, 160)}});
    }
    catch (...)
    {
      const auto failure = failures.fetch_add(1, std::memory_order_relaxed) + 1;
      p->drops.fetch_add(1, std::memory_order_relaxed);
      if (failure == 1)
        emit_diagnostic_event("videoSenderFailed",
                              {{"deviceId", p->device_id},
                               {"mediaSessionId", p->media_session_id},
                               {"track", track},
                               {"attempt", attempt},
                               {"reason", "unknown RTP sender failure"}});
    }
  }
}
void WebRtcHub::telemetry_loop()
{
  std::uint64_t tick = 0;
  while (!stopping_)
  {
    std::vector<std::pair<std::string, std::shared_ptr<Peer>>> removals;
    {
      std::lock_guard lock(mutex_);
      for (const auto& [id, peer] : peers_)
        if (peer->removal_requested.load(std::memory_order_acquire))
          removals.emplace_back(id, peer);
    }
    for (const auto& [id, peer] : removals)
      remove_peer(id, peer);
    std::vector<std::shared_ptr<Peer>> peers;
    {
      std::lock_guard lock(mutex_);
      for (auto& [_, p] : peers_)
        peers.push_back(p);
    }
    const bool meter_requested = std::any_of(
        peers.begin(), peers.end(), [](const auto& peer) { return peer->mixer_open.load(); });
    const auto meter_frame = state_.meter_frame(meter_requested);
    const auto meter = meter_frame ? meter_frame->dump() : std::string{};
    constexpr auto telemetry_tick_ms = 50U;
    static_assert(performance_update_interval_ms % telemetry_tick_ms == 0);
    constexpr auto performance_tick_interval = performance_update_interval_ms / telemetry_tick_ms;
    const auto performance_frame =
        tick % performance_tick_interval == 0 ? state_.generic_performance() : std::nullopt;
    const auto performance = performance_frame ? performance_frame->dump() : std::string{};
    const auto level = audio_level_telemetry_.latest();
    const auto master_level = audio_master_level_telemetry_.latest();
    for (auto& p : peers)
    {
      if (!p->telemetry || !p->telemetry->isOpen())
        continue;
      if (performance_frame)
        try
        {
          p->telemetry->send(performance);
        }
        catch (...)
        {
          ++p->drops;
        }
      if (meter_frame && p->mixer_open)
        try
        {
          p->telemetry->send(meter);
        }
        catch (...)
        {
          ++p->drops;
        }
      if (p->audio_monitor && level)
        try
        {
          p->telemetry->send(json{
              {"type", "audioLevel"},
              {"protocolVersion", 1},
              {"epoch", p->audio_epoch},
              {"sequence", level->sequence},
              {"sourceSampleIndex", level->source_sample_index},
              {"rmsDbfs", level->rms_dbfs},
              {"peakDbfs",
               level->peak_dbfs}}.dump());
        }
        catch (...)
        {
          ++p->drops;
        }
      if (p->audio_master && master_level)
        try
        {
          p->telemetry->send(json{{"type", "audioMasterLevel"},
                                  {"protocolVersion", 1},
                                  {"epoch", p->audio_epoch},
                                  {"sequence", master_level->sequence},
                                  {"sourceSampleIndex", master_level->source_sample_index},
                                  {"rmsDbfs", master_level->rms_dbfs},
                                  {"peakDbfs", master_level->peak_dbfs}}
                                 .dump());
        }
        catch (...)
        {
          ++p->drops;
        }
      if (tick != 0 && tick % 20 == 0 && p->audio_monitor && p->audio_egress_observer)
        try
        {
          const auto egress = p->audio_egress_observer->snapshot();
          p->telemetry->send(json{
              {"type", "audioMonitorEgress"},
              {"protocolVersion", 1},
              {"epoch", p->audio_epoch},
              {"oneSecondWindow", egress.window},
              {"egressPackets", egress.packets},
              {"firstSeq", egress.first_sequence},
              {"lastSeq", egress.last_sequence},
              {"firstRtpTimestamp", egress.first_rtp_timestamp},
              {"lastRtpTimestamp", egress.last_rtp_timestamp},
              {"interPacketP50Ms", egress.inter_packet_p50_us / 1'000.0},
              {"interPacketP95Ms", egress.inter_packet_p95_us / 1'000.0},
              {"interPacketMaxMs", egress.inter_packet_max_us / 1'000.0},
              {"interPacketOver40Ms", egress.inter_packet_over_40_ms},
              {"rtpContinuityViolations", egress.continuity_violations},
              {"encoderLateCount", audio_pacing_late_frames_.load(std::memory_order_relaxed)},
              {"encoderDurationP95Ms",
               audio_encode_p95_us_.load(std::memory_order_relaxed) / 1'000.0},
              {"q3OccupancyHighWater", p->encoded_audio_frames.high_watermark()},
              {"q3DropOldest", p->audio_sender_queue_drops.load(std::memory_order_relaxed)},
              {"senderSequenceSkips",
               p->audio_sender_sequence_skips.load(std::memory_order_relaxed)},
              {"sendDurationP95Ms", p->audio_send_p95_us.load(std::memory_order_relaxed) / 1'000.0},
              {"sendExceptions", p->audio_send_exceptions.load(std::memory_order_relaxed)},
              {"srSentCount", egress.sender_reports},
              {"lastSrRtpTimestamp", egress.last_sender_report_rtp_timestamp},
              {"rrFractionLost", p->audio_fraction_lost.load(std::memory_order_relaxed)},
              {"rrCumulativeLost", p->audio_packets_lost.load(std::memory_order_relaxed)},
              {"rrExtendedHighestSeq",
               p->audio_extended_highest_sequence.load(std::memory_order_relaxed)},
              {"rrJitter", p->audio_jitter.load(std::memory_order_relaxed)},
              {"rrRttMs", p->audio_round_trip_ms.load(std::memory_order_relaxed)},
              {"nackRequests", p->audio_nacks_received.load(std::memory_order_relaxed)},
              {"nackRetransmits", p->audio_nacks_retransmitted.load(std::memory_order_relaxed)},
              {"dscp",
               egress.dscp}}.dump());
        }
        catch (...)
        {
          ++p->drops;
        }
    }
    ++tick;
    for (int i = 0; i < 5 && !stopping_; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}
json WebRtcHub::health() const
{
  std::lock_guard lock(mutex_);
  const auto latency_json = [](const FixedLatencyHistogram& histogram)
  {
    const auto buckets = histogram.buckets();
    return json{{"le250us", buckets[0]},
                {"le500us", buckets[1]},
                {"le1ms", buckets[2]},
                {"le2ms", buckets[3]},
                {"le3ms", buckets[4]},
                {"le5ms", buckets[5]},
                {"le10ms", buckets[6]},
                {"le20ms", buckets[7]},
                {"over20ms", buckets[8]},
                {"maximumUs", histogram.maximum_us()}};
  };
  json peers = json::array();
  for (auto& [id, p] : peers_)
  {
    const auto track_health = [](const std::shared_ptr<rtc::Track>& track,
                                 const std::shared_ptr<Vp8RtpPacketizer>& packetizer,
                                 const std::atomic<std::uint64_t>& source_frames,
                                 const std::atomic<std::uint64_t>& encoder_inputs,
                                 const std::atomic<std::uint64_t>& frames,
                                 const std::atomic<std::uint64_t>& send_attempts,
                                 const std::atomic<std::uint64_t>& send_failures,
                                 const std::atomic<std::uint64_t>& rtcp_feedback,
                                 const std::atomic<std::uint64_t>& last_timestamp,
                                 const VideoTrackPreferences& preferences)
    {
      return json{{"negotiated", track != nullptr},
                  {"open", track && track->isOpen()},
                  {"targetFps", preferences.target_fps},
                  {"maxBitrateKbps", preferences.max_bitrate_kbps},
                  {"sourceFrames", source_frames.load(std::memory_order_relaxed)},
                  {"encoderInputFrames", encoder_inputs.load(std::memory_order_relaxed)},
                  {"encodedFrames", frames.load(std::memory_order_relaxed)},
                  {"sendAttempts", send_attempts.load(std::memory_order_relaxed)},
                  {"sendFailures", send_failures.load(std::memory_order_relaxed)},
                  {"packets", packetizer ? packetizer->packet_count() : 0},
                  {"bytes", packetizer ? packetizer->byte_count() : 0},
                  {"rtcpFeedback", rtcp_feedback.load(std::memory_order_relaxed)},
                  {"lastRtpTimestamp", last_timestamp.load(std::memory_order_relaxed)}};
    };
    const auto program = track_health(p->program,
                                      p->program_packetizer,
                                      p->program_source_frames,
                                      p->program_encoder_inputs,
                                      p->program_frames,
                                      p->program_send_attempts,
                                      p->program_send_failures,
                                      p->program_rtcp_feedback,
                                      p->program_last_timestamp,
                                      p->program_preferences);
    const auto preview = track_health(p->preview,
                                      p->preview_packetizer,
                                      p->preview_source_frames,
                                      p->preview_encoder_inputs,
                                      p->preview_frames,
                                      p->preview_send_attempts,
                                      p->preview_send_failures,
                                      p->preview_rtcp_feedback,
                                      p->preview_last_timestamp,
                                      p->preview_preferences);
    const auto egress =
        p->audio_egress_observer ? p->audio_egress_observer->snapshot() : AudioEgressWindow{};
    const json audio_monitor = {
        {"negotiated", p->audio_monitor != nullptr},
        {"open", p->audio_monitor && p->audio_monitor->isOpen()},
        {"ssrc", p->audio_monitor_ssrc},
        {"epoch", p->audio_epoch},
        {"sampleRate", ipc::audio_monitor_sample_rate},
        {"encodedPackets", p->audio_monitor_packets.load(std::memory_order_relaxed)},
        {"bytes", p->audio_monitor_bytes.load(std::memory_order_relaxed)},
        {"droppedFrames", p->audio_monitor_drops.load(std::memory_order_relaxed)},
        {"senderQueueCapacity", encoded_audio_queue_capacity},
        {"senderQueueOccupancy", p->encoded_audio_frames.size()},
        {"senderQueueHighWatermark", p->encoded_audio_frames.high_watermark()},
        {"senderQueueDrops", p->audio_sender_queue_drops.load(std::memory_order_relaxed)},
        {"senderExpiredDrops", p->audio_sender_expired_drops.load(std::memory_order_relaxed)},
        {"senderSequenceSkips", p->audio_sender_sequence_skips.load(std::memory_order_relaxed)},
        {"sendP99Us", p->audio_send_p99_us.load(std::memory_order_relaxed)},
        {"sendP95Us", p->audio_send_p95_us.load(std::memory_order_relaxed)},
        {"sendExceptions", p->audio_send_exceptions.load(std::memory_order_relaxed)},
        {"lastSendError", p->last_send_error()},
        {"lastDepartureIntervalUs",
         p->audio_last_departure_interval_us.load(std::memory_order_relaxed)},
        {"sendThreadCycles", p->audio_send_cycles.load(std::memory_order_relaxed)},
        {"senderMmcssFailures", p->audio_mmcss_failures.load(std::memory_order_relaxed)},
        {"overloadState",
         audio_overload_state_name(static_cast<AudioOverloadState>(
             p->audio_overload_state.load(std::memory_order_relaxed)))},
        {"overloadProfileStep", p->audio_profile_step.load(std::memory_order_relaxed)},
        {"rtcpReceiverReports", p->audio_rtcp_reports.load(std::memory_order_relaxed)},
        {"rtcpFractionLost", p->audio_fraction_lost.load(std::memory_order_relaxed)},
        {"rtcpPacketsLost", p->audio_packets_lost.load(std::memory_order_relaxed)},
        {"rtcpJitterSamples", p->audio_jitter.load(std::memory_order_relaxed)},
        {"rtcpRoundTripMs", p->audio_round_trip_ms.load(std::memory_order_relaxed)},
        {"rtcpExtendedHighestSequence",
         p->audio_extended_highest_sequence.load(std::memory_order_relaxed)},
        {"nacksReceived", p->audio_nacks_received.load(std::memory_order_relaxed)},
        {"nacksRetransmitted", p->audio_nacks_retransmitted.load(std::memory_order_relaxed)},
        {"nackDeadlineMisses", p->audio_nack_deadline_misses.load(std::memory_order_relaxed)},
        {"nackSendFailures", p->audio_nack_send_failures.load(std::memory_order_relaxed)},
        {"nackCachePackets", AudioNackResponder::retention_packets},
        {"egress",
         {{"oneSecondWindow", egress.window},
          {"packets", egress.packets},
          {"firstSequence", egress.first_sequence},
          {"lastSequence", egress.last_sequence},
          {"firstRtpTimestamp", egress.first_rtp_timestamp},
          {"lastRtpTimestamp", egress.last_rtp_timestamp},
          {"interPacketP50Us", egress.inter_packet_p50_us},
          {"interPacketP95Us", egress.inter_packet_p95_us},
          {"interPacketMaximumUs", egress.inter_packet_max_us},
          {"interPacketOver40Ms", egress.inter_packet_over_40_ms},
          {"rtpContinuityViolations", egress.continuity_violations},
          {"senderReports", egress.sender_reports},
          {"lastSenderReportRtpTimestamp", egress.last_sender_report_rtp_timestamp},
          {"dscp", egress.dscp}}},
        {"androidPlaybackReports", p->android_audio_reports.load(std::memory_order_relaxed)},
        {"androidPlcFrames", p->android_plc_frames.load(std::memory_order_relaxed)},
        {"androidAudioTrackUnderruns",
         p->android_audio_track_underruns.load(std::memory_order_relaxed)},
        {"androidOutputFrames", p->android_output_frames.load(std::memory_order_relaxed)},
        {"androidJitterBufferDelayMs",
         p->android_jitter_buffer_delay_ms.load(std::memory_order_relaxed)}};
    const json audio_master = {
        {"negotiated", p->audio_master != nullptr},
        {"open", p->audio_master && p->audio_master->isOpen()},
        {"ssrc", p->audio_master_ssrc},
        {"sampleRate", ipc::audio_monitor_sample_rate},
        {"encodedPackets", p->audio_master_packets.load(std::memory_order_relaxed)},
        {"bytes", p->audio_master_bytes.load(std::memory_order_relaxed)},
        {"droppedFrames", p->audio_master_drops.load(std::memory_order_relaxed)},
        {"senderQueueCapacity", encoded_audio_queue_capacity},
        {"senderQueueOccupancy", p->encoded_audio_master_frames.size()},
        {"senderQueueHighWatermark", p->encoded_audio_master_frames.high_watermark()},
        {"senderSequenceSkips", p->audio_master_sequence_skips.load(std::memory_order_relaxed)}};
    peers.push_back(
        {{"deviceId", id},
         {"mediaSessionId", p->media_session_id},
         {"state", static_cast<int>(p->connection->state())},
         {"stateName", peer_state_name(p->connection->state())},
         {"iceState", static_cast<int>(p->connection->iceState())},
         {"program", program},
         {"preview", preview},
         {"audioMonitor", audio_monitor},
         {"audioMaster", audio_master},
         {"packets",
          program["packets"].get<std::uint64_t>() + preview["packets"].get<std::uint64_t>() +
              audio_monitor["encodedPackets"].get<std::uint64_t>() +
              audio_master["encodedPackets"].get<std::uint64_t>()},
         {"bytes",
          program["bytes"].get<std::uint64_t>() + preview["bytes"].get<std::uint64_t>() +
              audio_monitor["bytes"].get<std::uint64_t>() +
              audio_master["bytes"].get<std::uint64_t>()},
         {"drops", p->drops.load(std::memory_order_relaxed)}});
  }
  const auto monitor = audio_monitor_status_;
  const auto master = audio_master_status_;
  std::uint64_t encoded_audio_packets = 0;
  std::uint64_t encoded_master_packets = 0;
  std::uint64_t peer_sender_queue_drops = 0;
  std::uint64_t sender_sequence_skips = 0;
  bool audio_requested = false;
  bool master_requested = false;
  for (const auto& [_, peer] : peers_)
  {
    audio_requested = audio_requested || peer->audio_monitor != nullptr;
    master_requested = master_requested || peer->audio_master != nullptr;
    encoded_audio_packets += peer->audio_monitor_packets.load(std::memory_order_relaxed);
    encoded_master_packets += peer->audio_master_packets.load(std::memory_order_relaxed);
    peer_sender_queue_drops += peer->audio_sender_queue_drops.load(std::memory_order_relaxed);
    sender_sequence_skips += peer->audio_sender_sequence_skips.load(std::memory_order_relaxed);
  }
  return {
      {"peerCount", peers_.size()},
      {"retainedVideoPolicies",
       {{"program", retained_program_policies_.load()},
        {"preview", retained_preview_policies_.load()}}},
      {"videoEncoderFailures",
       {{"program", program_encoder_failures_.load(std::memory_order_relaxed)},
        {"preview", preview_encoder_failures_.load(std::memory_order_relaxed)}}},
      {"audioMonitor",
       {{"available", monitor.available && !audio_encoder_failed_},
        {"requested", audio_requested},
        {"configured", monitor.configured},
        {"reason",
         monitor.available && !audio_encoder_failed_
             ? json(nullptr)
             : json(monitor.reason.empty() ? "backend-unavailable" : monitor.reason)},
        {"sampleRate", monitor.sample_rate},
        {"channels", monitor.channels},
        {"encodedPackets", encoded_audio_packets},
        {"producerDroppedFrames", monitor.dropped_frames},
        {"ingressOverflowDrops", monitor.ingress_overflow_drops},
        {"oversizedCallbackDrops", monitor.oversized_callback_drops},
        {"callbackCount", monitor.callback_count},
        {"callbackFrameHistogram",
         {{"le256", monitor.callback_frames_le_256},
          {"le512", monitor.callback_frames_le_512},
          {"le1024", monitor.callback_frames_le_1024},
          {"le2048", monitor.callback_frames_le_2048}}},
        {"maximumIngressOccupancy", monitor.maximum_ingress_occupancy},
        {"mixerDeadlineMisses", monitor.mixer_deadline_misses},
        {"producerTimelineGaps", monitor.timeline_gaps},
        {"producerTimelineOverlaps", monitor.timeline_overlaps},
        {"producerNonFiniteSamples", monitor.non_finite_samples},
        {"producerLimiterFrames", monitor.limiter_frames},
        {"latestPeak", monitor.latest_peak},
        {"latestRms", monitor.latest_rms},
        {"latestDc", monitor.latest_dc},
        {"ipcSkippedFrames", monitor.ipc_skipped_frames},
        {"ipcSampleIndexGaps", monitor.ipc_sample_index_gaps},
        {"ipcSeqlockRetries", monitor.ipc_seqlock_retries},
        {"producerTimestampDiscontinuities", monitor.timestamp_discontinuities},
        {"encoderQueueDrops", audio_queue_drops_.load(std::memory_order_relaxed)},
        {"encoderQueueCapacity", 3},
        {"encoderQueueOccupancy", audio_frames_.size()},
        {"senderQueueCapacity", encoded_audio_queue_capacity},
        {"senderQueueOccupancy", encoded_audio_frames_.size()},
        {"senderQueueHighWatermark", encoded_audio_frames_.high_watermark()},
        {"senderQueueDrops",
         audio_sender_queue_drops_.load(std::memory_order_relaxed) + peer_sender_queue_drops},
        {"senderExpiredDrops", audio_sender_expired_drops_.load(std::memory_order_relaxed)},
        {"senderSequenceSkips", sender_sequence_skips},
        {"sendExceptions", audio_send_exceptions_.load(std::memory_order_relaxed)},
        {"encoderInputSequenceGaps", audio_input_sequence_gaps_.load(std::memory_order_relaxed)},
        {"encoderTimestampDiscontinuities",
         audio_timestamp_discontinuities_.load(std::memory_order_relaxed)},
        {"opusFailures", audio_opus_failures_.load(std::memory_order_relaxed)},
        {"opusPackets", audio_encoded_packets_.load(std::memory_order_relaxed)},
        {"opusLookaheadSamples", audio_opus_lookahead_.load(std::memory_order_relaxed)},
        {"complexityBenchmarkComplete", audio_benchmark_complete_.load(std::memory_order_relaxed)},
        {"normalComplexity", audio_normal_complexity_.load(std::memory_order_relaxed)},
        {"complexityBenchmark",
         {{{"complexity", 0},
           {"p99Us", audio_complexity_p99_us_[0].load(std::memory_order_relaxed)},
           {"p99Cycles", audio_complexity_p99_cycles_[0].load(std::memory_order_relaxed)}},
          {{"complexity", 3},
           {"p99Us", audio_complexity_p99_us_[1].load(std::memory_order_relaxed)},
           {"p99Cycles", audio_complexity_p99_cycles_[1].load(std::memory_order_relaxed)}},
          {{"complexity", 6},
           {"p99Us", audio_complexity_p99_us_[2].load(std::memory_order_relaxed)},
           {"p99Cycles", audio_complexity_p99_cycles_[2].load(std::memory_order_relaxed)}},
          {{"complexity", 10},
           {"p99Us", audio_complexity_p99_us_[3].load(std::memory_order_relaxed)},
           {"p99Cycles", audio_complexity_p99_cycles_[3].load(std::memory_order_relaxed)}}}},
        {"overloadState",
         audio_overload_state_name(static_cast<AudioOverloadState>(
             audio_overload_state_.load(std::memory_order_relaxed)))},
        {"overloadProfileStep", audio_profile_step_.load(std::memory_order_relaxed)},
        {"overloadTransitions", audio_overload_transitions_.load(std::memory_order_relaxed)},
        {"encodeP99Us", audio_encode_p99_us_.load(std::memory_order_relaxed)},
        {"encodeP95Us", audio_encode_p95_us_.load(std::memory_order_relaxed)},
        {"sendP99Us", audio_send_p99_us_.load(std::memory_order_relaxed)},
        {"encodeLatency", latency_json(audio_encode_latency_)},
        {"sendLatency", latency_json(audio_send_latency_)},
        {"departureInterval", latency_json(audio_departure_interval_)},
        {"departureBurstsUnder10Ms", audio_departure_bursts_.load(std::memory_order_relaxed)},
        {"departureGapsOver30Ms", audio_departure_gaps_.load(std::memory_order_relaxed)},
        {"encodeThreadCycles", audio_encode_cycles_.load(std::memory_order_relaxed)},
        {"sendThreadCycles", audio_send_cycles_.load(std::memory_order_relaxed)},
        {"timingRecords", audio_frame_telemetry_.published()},
        {"encoderMmcssFailures", audio_encoder_mmcss_failures_.load(std::memory_order_relaxed)},
        {"senderMmcssFailures", audio_sender_mmcss_failures_.load(std::memory_order_relaxed)},
        {"processHighQos", realtime_audio_ && realtime_audio_->high_qos_enabled()},
        {"pacingResets", audio_pacing_resets_.load(std::memory_order_relaxed)},
        {"pacingLateFrames", audio_pacing_late_frames_.load(std::memory_order_relaxed)},
        {"maximumPacingLatenessUs",
         audio_maximum_pacing_lateness_us_.load(std::memory_order_relaxed)},
        {"postOpusDiagnosticDecodeFailures",
         post_opus_diagnostic_decode_failures_.load(std::memory_order_relaxed)},
        {"ageMs", monitor.age_ms}}},
      {"audioMaster",
       {{"available", master.available && !audio_master_encoder_failed_},
        {"requested", master_requested},
        {"configured", master.configured},
        {"reason",
         master.available && !audio_master_encoder_failed_
             ? json(nullptr)
             : json(master.reason.empty() ? "backend-unavailable" : master.reason)},
        {"sampleRate", master.sample_rate},
        {"channels", master.channels},
        {"encodedPackets", encoded_master_packets},
        {"producerDroppedFrames", master.dropped_frames},
        {"callbackCount", master.callback_count},
        {"encoderQueueDrops", audio_master_queue_drops_.load(std::memory_order_relaxed)},
        {"encoderQueueCapacity", 3},
        {"encoderQueueOccupancy", audio_master_frames_.size()},
        {"opusPackets", audio_master_encoded_packets_.load(std::memory_order_relaxed)},
        {"ageMs", master.age_ms}}},
      {"peers", peers}};
}
} // namespace studio_remote
