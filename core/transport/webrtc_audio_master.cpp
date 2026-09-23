#include "libdatachannel_peer.hpp"
#include "libdatachannel_transport.hpp"

#include <studio_remote/ipc/audio_monitor_mapping.hpp>

#include <algorithm>
#include <chrono>
#include <opus/opus.h>
#include <thread>

namespace studio_remote
{

void WebRtcHub::audio_master_encode_loop()
{
  OpusEncoder* encoder = nullptr;
  std::uint64_t frame_index = 0;
  auto registration =
      realtime_audio_ ? realtime_audio_->register_current_thread(RealtimeAudioRole::opus_encoder)
                      : nullptr;
  static_cast<void>(registration);
  while (!stopping_)
  {
    SharedAudioMonitorFrame frame;
    {
      std::unique_lock lock(mutex_);
      frame_cv_.wait_for(lock,
                         std::chrono::milliseconds(100),
                         [&] { return stopping_ || audio_master_frames_.size() != 0; });
      if (stopping_)
        break;
      frame = audio_master_frames_.pop();
      const bool requested =
          std::any_of(peers_.begin(),
                      peers_.end(),
                      [](const auto& entry) { return entry.second->audio_master != nullptr; });
      if (!requested)
      {
        audio_master_frames_.clear();
        frame.reset();
      }
    }
    if (!frame || frame->sample_rate != ipc::audio_monitor_sample_rate || frame->channels != 2 ||
        frame->samples.size() != ipc::audio_monitor_frame_samples * frame->channels)
      continue;

    if (!encoder)
    {
      int error = OPUS_OK;
      encoder =
          opus_encoder_create(ipc::audio_monitor_sample_rate, 2, OPUS_APPLICATION_AUDIO, &error);
      if (!encoder || error != OPUS_OK)
      {
        encoder = nullptr;
        audio_master_encoder_failed_.store(true, std::memory_order_release);
        continue;
      }
      const bool configured = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(128'000)) == OPUS_OK &&
                              opus_encoder_ctl(encoder, OPUS_SET_VBR(1)) == OPUS_OK &&
                              opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(1)) == OPUS_OK &&
                              opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(6)) == OPUS_OK &&
                              opus_encoder_ctl(encoder, OPUS_SET_DTX(0)) == OPUS_OK &&
                              opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(0)) == OPUS_OK;
      if (!configured)
      {
        opus_encoder_destroy(encoder);
        encoder = nullptr;
        audio_master_encoder_failed_.store(true, std::memory_order_release);
        continue;
      }
    }

    EncodedOpusFrame encoded;
    encoded.frame_index = ++frame_index;
    encoded.source_sequence = frame->sequence;
    encoded.first_sample_index = frame->first_sample_index;
    encoded.topology_generation = frame->topology_generation;
    encoded.rtp_timestamp = static_cast<std::uint32_t>(frame->first_sample_index);
    encoded.flags = frame->flags;
    encoded.enqueue_send_ticks = performance_counter();
    const auto encoded_size =
        opus_encode_float(encoder,
                          frame->samples.data(),
                          ipc::audio_monitor_frame_samples,
                          reinterpret_cast<unsigned char*>(encoded.payload.data()),
                          static_cast<opus_int32>(encoded.payload.size()));
    if (encoded_size <= 0)
    {
      audio_master_encoder_failed_.store(true, std::memory_order_release);
      continue;
    }
    encoded.payload_bytes = static_cast<std::uint16_t>(encoded_size);
    audio_master_encoder_failed_.store(false, std::memory_order_release);
    audio_master_encoded_packets_.fetch_add(1, std::memory_order_relaxed);

    std::vector<std::shared_ptr<Peer>> peers;
    {
      std::lock_guard lock(mutex_);
      for (const auto& [_, peer] : peers_)
        peers.push_back(peer);
    }
    for (const auto& peer : peers)
    {
      if (!peer->audio_master || !peer->audio_master->isOpen())
        continue;
      peer->audio_master_drops.fetch_add(peer->encoded_audio_master_frames.push(encoded),
                                         std::memory_order_relaxed);
      peer->audio_master_cv.notify_one();
    }
  }
  if (encoder)
    opus_encoder_destroy(encoder);
}

void WebRtcHub::audio_master_peer_sender_loop(Peer& peer)
{
  auto registration = realtime_audio_
                          ? realtime_audio_->register_current_thread(RealtimeAudioRole::rtp_sender)
                          : nullptr;
  static_cast<void>(registration);
  auto next_send = std::chrono::steady_clock::now();
  while (!stopping_ && !peer.audio_master_stopping.load(std::memory_order_acquire))
  {
    {
      std::unique_lock lock(peer.audio_master_wait_mutex);
      peer.audio_master_cv.wait_for(
          lock,
          std::chrono::milliseconds(100),
          [&]
          {
            return stopping_ || peer.audio_master_stopping.load(std::memory_order_acquire) ||
                   peer.encoded_audio_master_frames.size() != 0;
          });
    }
    if (stopping_ || peer.audio_master_stopping.load(std::memory_order_acquire))
      break;
    auto encoded = peer.encoded_audio_master_frames.pop();
    if (!encoded)
      continue;
    if (next_send > std::chrono::steady_clock::now())
      std::this_thread::sleep_until(next_send);
    next_send =
        std::max(next_send + std::chrono::milliseconds(20), std::chrono::steady_clock::now());

    const auto previous = peer.audio_master_last_rtp_timestamp.load(std::memory_order_relaxed);
    if (peer.audio_master_rtp_timeline_started.load(std::memory_order_relaxed) &&
        peer.audio_master_packetizer)
    {
      const auto skipped = audio_rtp_sequence_skips(previous, encoded->rtp_timestamp);
      if (skipped != 0)
      {
        auto& sequence = peer.audio_master_packetizer->rtpConfig->sequenceNumber;
        sequence = static_cast<std::uint16_t>(sequence + skipped);
        peer.audio_master_sequence_skips.fetch_add(skipped, std::memory_order_relaxed);
      }
    }
    peer.audio_master_last_rtp_timestamp.store(encoded->rtp_timestamp, std::memory_order_relaxed);
    peer.audio_master_rtp_timeline_started.store(true, std::memory_order_relaxed);
    try
    {
      peer.audio_master->sendFrame(
          encoded->payload.data(), encoded->payload_bytes, rtc::FrameInfo(encoded->rtp_timestamp));
      peer.audio_master_packets.fetch_add(1, std::memory_order_relaxed);
      peer.audio_master_bytes.fetch_add(encoded->payload_bytes, std::memory_order_relaxed);
    }
    catch (const std::exception& error)
    {
      peer.record_send_error(error.what());
      peer.audio_master_drops.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

} // namespace studio_remote
