#include "windows_mpeg_ts_video_decoder.hpp"
#include "srt_sequential_stream.hpp"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <stdexcept>
#include <string_view>

namespace studio_remote::windows
{
namespace
{
constexpr int avio_buffer_bytes = 64 * 1024;

std::string ffmpeg_error(int error, std::string_view operation)
{
  char detail[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(error, detail, sizeof(detail));
  return std::string(operation) + ": " + detail;
}

void require_ffmpeg(int result, std::string_view operation)
{
  if (result < 0)
    throw std::runtime_error(ffmpeg_error(result, operation));
}

std::uint64_t monotonic_nanoseconds()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count());
}

int read_srt(void* opaque, std::uint8_t* buffer, int bytes)
{
  auto& stream = *static_cast<SrtSequentialStream*>(opaque);
  const auto received = stream.read(buffer, bytes);
  if (received > 0)
    return received;
  return stream.cancelled() ? AVERROR_EXIT : AVERROR(EIO);
}

int interrupt_ffmpeg(void* opaque)
{
  return static_cast<SrtSequentialStream*>(opaque)->cancelled() ? 1 : 0;
}
} // namespace

struct WindowsMpegTsVideoDecoder::Impl
{
  explicit Impl(SrtSequentialStream* source) : stream(source)
  {
  }

  ~Impl()
  {
    if (packet)
      av_packet_free(&packet);
    if (decoded)
      av_frame_free(&decoded);
    if (codec)
      avcodec_free_context(&codec);
    if (format)
      avformat_close_input(&format);
    if (avio)
      avio_context_free(&avio);
    if (scaler)
      sws_freeContext(scaler);
  }

  SrtSequentialStream* stream;
  AVIOContext* avio = nullptr;
  AVFormatContext* format = nullptr;
  AVCodecContext* codec = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* decoded = nullptr;
  SwsContext* scaler = nullptr;
  int video_stream = -1;
};

WindowsMpegTsVideoDecoder::WindowsMpegTsVideoDecoder(SrtSequentialStream* transport_stream)
    : implementation_(std::make_unique<Impl>(transport_stream))
{
  if (!transport_stream)
    throw std::invalid_argument("An SRT transport stream is required");

  auto* avio_buffer = static_cast<unsigned char*>(av_malloc(avio_buffer_bytes));
  if (!avio_buffer)
    throw std::bad_alloc();
  implementation_->avio = avio_alloc_context(
      avio_buffer, avio_buffer_bytes, 0, transport_stream, read_srt, nullptr, nullptr);
  if (!implementation_->avio)
  {
    av_free(avio_buffer);
    throw std::bad_alloc();
  }

  implementation_->format = avformat_alloc_context();
  if (!implementation_->format)
    throw std::bad_alloc();
  implementation_->format->pb = implementation_->avio;
  implementation_->format->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_NOBUFFER;
  implementation_->format->interrupt_callback = {interrupt_ffmpeg, transport_stream};
  const auto* input_format = av_find_input_format("mpegts");
  if (!input_format)
    throw std::runtime_error("FFmpeg MPEG-TS demuxer is unavailable");
  require_ffmpeg(avformat_open_input(&implementation_->format, nullptr, input_format, nullptr),
                 "Open MPEG-TS stream");
  require_ffmpeg(avformat_find_stream_info(implementation_->format, nullptr),
                 "Read MPEG-TS stream information");

  const AVCodec* decoder = nullptr;
  implementation_->video_stream =
      av_find_best_stream(implementation_->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
  require_ffmpeg(implementation_->video_stream, "Find MPEG-TS video stream");
  if (!decoder)
    throw std::runtime_error("FFmpeg H.264 decoder is unavailable");
  implementation_->codec = avcodec_alloc_context3(decoder);
  if (!implementation_->codec)
    throw std::bad_alloc();
  require_ffmpeg(avcodec_parameters_to_context(
                     implementation_->codec,
                     implementation_->format->streams[implementation_->video_stream]->codecpar),
                 "Configure H.264 decoder");
  implementation_->codec->flags |= AV_CODEC_FLAG_LOW_DELAY;
  implementation_->codec->thread_count = 1;
  require_ffmpeg(avcodec_open2(implementation_->codec, decoder, nullptr), "Open H.264 decoder");
  implementation_->packet = av_packet_alloc();
  implementation_->decoded = av_frame_alloc();
  if (!implementation_->packet || !implementation_->decoded)
    throw std::bad_alloc();
}

WindowsMpegTsVideoDecoder::~WindowsMpegTsVideoDecoder() = default;

std::shared_ptr<RawVideoFrame> WindowsMpegTsVideoDecoder::read_frame()
{
  auto& state = *implementation_;
  while (!state.stream->cancelled())
  {
    const auto decoded_result = avcodec_receive_frame(state.codec, state.decoded);
    if (decoded_result == 0)
    {
      auto frame = std::make_shared<RawVideoFrame>();
      frame->width = static_cast<std::uint32_t>(state.decoded->width);
      frame->height = static_cast<std::uint32_t>(state.decoded->height);
      frame->stride = frame->width * 4U;
      frame->captured_ns = monotonic_nanoseconds();
      frame->bgra.resize(static_cast<std::size_t>(frame->stride) * frame->height);
      state.scaler = sws_getCachedContext(state.scaler,
                                          state.decoded->width,
                                          state.decoded->height,
                                          static_cast<AVPixelFormat>(state.decoded->format),
                                          state.decoded->width,
                                          state.decoded->height,
                                          AV_PIX_FMT_BGRA,
                                          SWS_FAST_BILINEAR,
                                          nullptr,
                                          nullptr,
                                          nullptr);
      if (!state.scaler)
        throw std::runtime_error("Create FFmpeg BGRA converter failed");
      std::uint8_t* destination[] = {frame->bgra.data(), nullptr, nullptr, nullptr};
      const int destination_stride[] = {static_cast<int>(frame->stride), 0, 0, 0};
      const auto rows = sws_scale(state.scaler,
                                  state.decoded->data,
                                  state.decoded->linesize,
                                  0,
                                  state.decoded->height,
                                  destination,
                                  destination_stride);
      if (rows != state.decoded->height)
        throw std::runtime_error("Convert FFmpeg video frame failed");
      av_frame_unref(state.decoded);
      return frame;
    }
    if (decoded_result != AVERROR(EAGAIN) && decoded_result != AVERROR_EOF)
      throw std::runtime_error(ffmpeg_error(decoded_result, "Decode H.264 frame"));

    int read_result = 0;
    do
    {
      av_packet_unref(state.packet);
      read_result = av_read_frame(state.format, state.packet);
      if (read_result < 0)
      {
        if (state.stream->cancelled())
          return {};
        throw std::runtime_error(ffmpeg_error(read_result, "Read MPEG-TS packet"));
      }
    } while (state.packet->stream_index != state.video_stream);
    const auto send_result = avcodec_send_packet(state.codec, state.packet);
    av_packet_unref(state.packet);
    if (send_result < 0 && send_result != AVERROR(EAGAIN))
      throw std::runtime_error(ffmpeg_error(send_result, "Submit H.264 packet"));
  }
  return {};
}

} // namespace studio_remote::windows
