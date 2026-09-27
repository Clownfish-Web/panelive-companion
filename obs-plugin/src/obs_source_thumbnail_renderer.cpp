#include "obs_source_thumbnail_renderer.hpp"
#include "thumbnail_frame_readiness.hpp"

#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace studio_remote::obs_plugin
{
namespace
{
constexpr std::uint32_t minimum_warmup_cycles = 15;
constexpr std::uint32_t retry_interval_cycles = 6;
constexpr std::uint32_t maximum_warmup_cycles = 120;
constexpr std::uint64_t maximum_warmup_ns = 2'000'000'000ULL;
using GetSourceByUuid = obs_source_t* (*)(const char*);
using GetSourceUuid = const char* (*)(const obs_source_t*);

GetSourceByUuid get_source_by_uuid()
{
#ifdef _WIN32
  const auto module = GetModuleHandleW(L"obs.dll");
  return module
             ? reinterpret_cast<GetSourceByUuid>(GetProcAddress(module, "obs_get_source_by_uuid"))
             : nullptr;
#else
  return &obs_get_source_by_uuid;
#endif
}

GetSourceUuid get_source_uuid()
{
#ifdef _WIN32
  const auto module = GetModuleHandleW(L"obs.dll");
  return module ? reinterpret_cast<GetSourceUuid>(GetProcAddress(module, "obs_source_get_uuid"))
                : nullptr;
#else
  return &obs_source_get_uuid;
#endif
}

ThumbnailSourceSettings thumbnail_source_settings(obs_source_t* source)
{
  ThumbnailSourceSettings result;
  obs_data_t* settings = obs_source_get_settings(source);
  if (!settings)
    return result;
  result.deactivate_when_not_showing = obs_data_get_bool(settings, "deactivate_when_not_showing") ||
                                       obs_data_get_bool(settings, "deactivateWhenNotShowing");
  result.shutdown_when_not_visible = obs_data_get_bool(settings, "shutdown") ||
                                     obs_data_get_bool(settings, "shutdown_on_invisible");
  obs_data_release(settings);
  return result;
}

std::pair<std::uint32_t, std::uint32_t> output_size(std::uint32_t width,
                                                    std::uint32_t height,
                                                    std::uint32_t maximum_width,
                                                    std::uint32_t maximum_height)
{
  const auto scale = std::min({1.0,
                               static_cast<double>(maximum_width) / width,
                               static_cast<double>(maximum_height) / height});
  return {std::max(1U, static_cast<std::uint32_t>(std::floor(width * scale))),
          std::max(1U, static_cast<std::uint32_t>(std::floor(height * scale)))};
}
} // namespace

ObsSourceThumbnailRenderer::ObsSourceThumbnailRenderer(SharedThumbnailMailbox& mailbox)
    : mailbox_(mailbox)
{
}

ObsSourceThumbnailRenderer::~ObsSourceThumbnailRenderer()
{
  stop();
}

bool ObsSourceThumbnailRenderer::start()
{
  if (started_)
    return true;
  obs_add_main_render_callback(render_callback, this);
  started_ = true;
  return true;
}

void ObsSourceThumbnailRenderer::stop()
{
  if (!started_)
    return;
  obs_remove_main_render_callback(render_callback, this);
  if (request_)
    finish_request(ipc::ThumbnailStatus::render_failed);
  started_ = false;
}

void ObsSourceThumbnailRenderer::render_callback(void* context, std::uint32_t, std::uint32_t)
{
  if (context)
  {
    auto* renderer = static_cast<ObsSourceThumbnailRenderer*>(context);
    try
    {
      renderer->render();
    }
    catch (...)
    {
      blog(LOG_ERROR, "[PaneLive Companion] Source thumbnail rendering failed unexpectedly");
      renderer->finish_request(ipc::ThumbnailStatus::render_failed);
    }
  }
}

void ObsSourceThumbnailRenderer::render()
{
  if (!request_)
  {
    if (auto request = mailbox_.request_after(last_request_sequence_))
      begin_request(std::move(*request));
    return;
  }

  ++render_cycles_;
  const auto now = os_gettime_ns();
  if (render_cycles_ < next_capture_cycle_ && !warmup_expired(now))
    return;

  ++capture_attempts_;
  const auto attempt = capture_source();
  if (attempt.source_missing)
  {
    finish_request(ipc::ThumbnailStatus::source_not_found);
    return;
  }
  if (attempt.source_unavailable)
  {
    blog(LOG_INFO,
         "[PaneLive Companion] Thumbnail unavailable id=%.8s kind=%s reason=zero_dimensions",
         request_->source_id.c_str(),
         source_kind_.c_str());
    finish_request(ipc::ThumbnailStatus::source_unavailable);
    return;
  }
  if (attempt.activation_required)
  {
    finish_request(ipc::ThumbnailStatus::activation_required);
    return;
  }
  const auto elapsed_ms = (now - activation_started_ns_) / 1'000'000ULL;
  const ThumbnailPixelStats stats{
      attempt.black, attempt.average_channel, attempt.non_black_pixels, attempt.total_pixels};
  const bool ready = attempt.copied && thumbnail_pixels_ready(source_kind_, stats);
  blog(LOG_INFO,
       "[PaneLive Companion] Thumbnail capture id=%.8s kind=%s attempt=%u cycle=%u "
       "elapsedMs=%llu averageChannel=%.3f nonBlackPixels=%zu/%zu result=%s",
       request_->source_id.c_str(),
       source_kind_.c_str(),
       capture_attempts_,
       render_cycles_,
       static_cast<unsigned long long>(elapsed_ms),
       attempt.average_channel,
       attempt.non_black_pixels,
       attempt.total_pixels,
       !attempt.copied ? "copy_failed"
       : attempt.black ? "initializing_black"
                       : "ready");

  if (ready)
  {
    finish_request(ipc::ThumbnailStatus::success);
    return;
  }
  if (!warmup_expired(now))
  {
    next_capture_cycle_ = render_cycles_ + retry_interval_cycles;
    return;
  }
  blog(LOG_WARNING,
       "[PaneLive Companion] Thumbnail warm-up timed out id=%.8s kind=%s attempts=%u cycles=%u "
       "elapsedMs=%llu",
       request_->source_id.c_str(),
       source_kind_.c_str(),
       capture_attempts_,
       render_cycles_,
       static_cast<unsigned long long>(elapsed_ms));
  finish_request(monitor_capture_ ? ipc::ThumbnailStatus::source_initialization_timeout
                                  : ipc::ThumbnailStatus::render_failed);
}

void ObsSourceThumbnailRenderer::begin_request(ThumbnailRequest request)
{
  last_request_sequence_ = request.sequence;
  if (request.maximum_width == 0 || request.maximum_height == 0 ||
      request.maximum_width > ipc::maximum_thumbnail_width ||
      request.maximum_height > ipc::maximum_thumbnail_height || request.source_name.empty())
  {
    mailbox_.publish(request.sequence, ipc::ThumbnailStatus::invalid_request);
    return;
  }

  obs_source_t* source = nullptr;
  if (!request.source_id.starts_with("legacy-"))
  {
    if (const auto lookup = get_source_by_uuid())
      source = lookup(request.source_id.c_str());
  }
  else
  {
    source = obs_get_source_by_name(request.source_name.c_str());
  }
  if (!source)
  {
    mailbox_.publish(request.sequence, ipc::ThumbnailStatus::source_not_found);
    return;
  }
  if (const auto uuid = get_source_uuid();
      uuid && !request.source_id.starts_with("legacy-") && request.source_id != uuid(source))
  {
    obs_source_release(source);
    mailbox_.publish(request.sequence, ipc::ThumbnailStatus::source_not_found);
    return;
  }
  const auto type = obs_source_get_type(source);
  if (type != OBS_SOURCE_TYPE_INPUT && type != OBS_SOURCE_TYPE_SCENE)
  {
    obs_source_release(source);
    mailbox_.publish(request.sequence, ipc::ThumbnailStatus::unsupported_source);
    return;
  }

  request_ = std::move(request);
  const auto* source_kind = obs_source_get_unversioned_id(source);
  source_kind_ = source_kind ? source_kind : "unknown";
  monitor_capture_ = source_kind_ == "monitor_capture";
  capture_policy_ = thumbnail_capture_policy(source_kind_, thumbnail_source_settings(source));
  source_ = obs_source_get_weak_source(source);
  const bool currently_showing = obs_source_showing(source);
  log_reference_state(source, "request_started", 0, 0);
  obs_source_release(source);
  if (!source_)
  {
    finish_request(ipc::ThumbnailStatus::source_not_found);
    return;
  }
  if (capture_policy_ == ThumbnailCapturePolicy::require_existing_showing && !currently_showing)
  {
    blog(LOG_INFO,
         "[PaneLive Companion] Thumbnail refused id=%.8s kind=%s reason=activation_required",
         request_->source_id.c_str(),
         source_kind_.c_str());
    finish_request(ipc::ThumbnailStatus::activation_required);
    return;
  }
  activation_started_ns_ = os_gettime_ns();
  render_cycles_ = 0;
  next_capture_cycle_ = minimum_warmup_cycles;
  capture_attempts_ = 0;
}

void ObsSourceThumbnailRenderer::finish_request(ipc::ThumbnailStatus status)
{
  if (!request_)
    return;
  if (status != ipc::ThumbnailStatus::success)
    mailbox_.publish(request_->sequence, status);
  if (source_)
    obs_weak_source_release(source_);
  source_ = nullptr;
  source_kind_.clear();
  monitor_capture_ = false;
  capture_policy_ = ThumbnailCapturePolicy::showing_only_render;
  request_.reset();
}

ObsSourceThumbnailRenderer::CaptureAttempt ObsSourceThumbnailRenderer::capture_source()
{
  CaptureAttempt attempt;
  obs_source_t* source = source_ ? obs_weak_source_get_source(source_) : nullptr;
  if (!source)
  {
    attempt.source_missing = true;
    return attempt;
  }
  if (capture_policy_ == ThumbnailCapturePolicy::require_existing_showing &&
      !obs_source_showing(source))
  {
    attempt.activation_required = true;
    obs_source_release(source);
    return attempt;
  }
  const auto source_width = obs_source_get_width(source);
  const auto source_height = obs_source_get_height(source);
  if (source_width == 0 || source_height == 0)
  {
    attempt.source_unavailable = true;
    obs_source_release(source);
    return attempt;
  }
  const auto [width, height] =
      output_size(source_width, source_height, request_->maximum_width, request_->maximum_height);
  const auto target_stride = width * ipc::thumbnail_bytes_per_pixel;
  std::vector<std::uint8_t> packed(static_cast<std::size_t>(target_stride) * height);

  auto* texture = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
  auto* stage = gs_stagesurface_create(width, height, GS_BGRA);
  if (!texture || !stage)
  {
    gs_texrender_destroy(texture);
    gs_stagesurface_destroy(stage);
    obs_source_release(source);
    return attempt;
  }

  if (gs_texrender_begin(texture, width, height))
  {
    vec4 clear{};
    vec4_zero(&clear);
    gs_clear(GS_CLEAR_COLOR, &clear, 0.0F, 0);
    gs_viewport_push();
    gs_projection_push();
    gs_ortho(0.0F,
             static_cast<float>(source_width),
             0.0F,
             static_cast<float>(source_height),
             -100.0F,
             100.0F);
    gs_set_viewport(0, 0, width, height);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    // Match OBS ScreenshotObj exactly: the AUX/showing reference brackets one render call and is
    // never retained across frames or warm-up retries. A MAIN_VIEW active reference is forbidden.
    log_reference_state(source, "before_render", 0, 0);
    obs_source_inc_showing(source);
    obs_source_video_render(source);
    obs_source_dec_showing(source);
    log_reference_state(source, "after_render", 0, 0);
    gs_blend_state_pop();
    gs_projection_pop();
    gs_viewport_pop();
    gs_texrender_end(texture);

    gs_stage_texture(stage, gs_texrender_get_texture(texture));
    std::uint8_t* pixels = nullptr;
    std::uint32_t stride = 0;
    if (gs_stagesurface_map(stage, &pixels, &stride))
    {
      for (std::uint32_t row = 0; row < height; ++row)
        std::memcpy(packed.data() + static_cast<std::size_t>(row) * target_stride,
                    pixels + static_cast<std::size_t>(row) * stride,
                    target_stride);
      gs_stagesurface_unmap(stage);
      const auto stats = analyze_thumbnail_pixels(packed);
      attempt.total_pixels = stats.total_pixels;
      attempt.non_black_pixels = stats.non_black_pixels;
      attempt.average_channel = stats.average_channel;
      attempt.black = stats.black;
      attempt.copied = true;
      if (thumbnail_pixels_ready(source_kind_, stats))
        mailbox_.publish(request_->sequence,
                         ipc::ThumbnailStatus::success,
                         packed,
                         width,
                         height,
                         target_stride,
                         os_gettime_ns());
    }
  }
  gs_stagesurface_destroy(stage);
  gs_texrender_destroy(texture);
  obs_source_release(source);
  return attempt;
}

bool ObsSourceThumbnailRenderer::warmup_expired(std::uint64_t now_ns) const
{
  return render_cycles_ >= maximum_warmup_cycles ||
         now_ns - activation_started_ns_ >= maximum_warmup_ns;
}

void ObsSourceThumbnailRenderer::log_reference_state(obs_source_t* source,
                                                     const char* phase,
                                                     std::uint32_t owned_active_references,
                                                     std::uint32_t owned_showing_references) const
{
  blog(LOG_INFO,
       "[PaneLive Companion] Thumbnail source refs phase=%s thread=obs-main-render id=%.8s kind=%s "
       "dimensions=%ux%u active=%d showing=%d flags=0x%08x ownedActiveRefs=%u "
       "ownedShowingRefs=%u",
       phase,
       request_->source_id.c_str(),
       source_kind_.c_str(),
       obs_source_get_width(source),
       obs_source_get_height(source),
       obs_source_active(source) ? 1 : 0,
       obs_source_showing(source) ? 1 : 0,
       obs_source_get_flags(source),
       owned_active_references,
       owned_showing_references);
}

} // namespace studio_remote::obs_plugin
