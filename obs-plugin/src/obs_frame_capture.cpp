#include "obs_frame_capture.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

namespace studio_remote::obs_plugin
{
namespace
{

std::pair<std::uint32_t, std::uint32_t> scaled_frame_size(std::uint32_t source_width,
                                                          std::uint32_t source_height)
{
  if (source_width == 0 || source_height == 0)
    return {0, 0};

  const double scale = std::min({1.0,
                                 static_cast<double>(ipc::maximum_frame_width) / source_width,
                                 static_cast<double>(ipc::maximum_frame_height) / source_height});
  const auto width = static_cast<std::uint32_t>(source_width * scale) & ~1U;
  const auto height = static_cast<std::uint32_t>(source_height * scale) & ~1U;
  return {std::max(2U, width), std::max(2U, height)};
}

video_scale_info bgra_conversion(std::uint32_t width, std::uint32_t height)
{
  video_scale_info conversion{};
  conversion.format = VIDEO_FORMAT_BGRA;
  conversion.width = width;
  conversion.height = height;
  conversion.range = VIDEO_RANGE_FULL;
  conversion.colorspace = VIDEO_CS_709;
  return conversion;
}

} // namespace

ObsFrameCapture::ObsFrameCapture(SharedFramePublisher& publisher)
    : publisher_(publisher), lifecycle_([this] { return activate_capture(); },
                                        [this] { deactivate_capture(); },
                                        [this](CompanionCaptureState) { publish_status(); }),
      preview_output_([this](video_data& frame) { on_preview_frame(frame); })
{
}

ObsFrameCapture::~ObsFrameCapture()
{
  stop();
}

bool ObsFrameCapture::start()
{
  if (started_)
    return true;

  obs_video_info video_info{};
  if (!obs_get_video_info(&video_info))
    return false;

  std::tie(width_, height_) = scaled_frame_size(video_info.base_width, video_info.base_height);
  if (width_ == 0 || height_ == 0)
    return false;

  program_conversion_ = bgra_conversion(width_, height_);
  preview_conversion_ = bgra_conversion(width_, height_);

  obs_frontend_add_event_callback(handle_frontend_event, this);
  frontend_callback_registered_ = true;
  started_ = true;
  publish_status();
  return true;
}

void ObsFrameCapture::stop()
{
  if (!started_)
    return;

  if (frontend_callback_registered_)
    obs_frontend_remove_event_callback(handle_frontend_event, this);
  frontend_callback_registered_ = false;
  lifecycle_.shutdown();
  started_ = false;
}

void ObsFrameCapture::poll_demand(std::uint64_t now_ns)
{
  if (!started_)
    return;
  const auto newest = publisher_.demand_snapshot(now_ns);
  const bool changed = newest.generation != demand_.generation ||
                       newest.heartbeat_fresh != demand_.heartbeat_fresh ||
                       newest.demand != demand_.demand;
  demand_ = newest;
  requested_program_fps_.store(demand_.demand.requested_program_fps, std::memory_order_release);
  requested_preview_fps_.store(demand_.demand.requested_preview_fps, std::memory_order_release);
  const bool demanded = demand_.heartbeat_fresh && demand_.demand.active_video_client_count != 0;
  if (reload_completion_pending_)
  {
    // Frontend notifications run before the event dispatcher returns to its normal UI loop.
    // Recreate outputs from the next timer poll, after OBS completes the collection or profile
    // switch.
    reload_completion_pending_ = false;
    lifecycle_.complete_capture_reload(demanded, demand_.generation);
  }
  else
  {
    lifecycle_.reconcile(demanded, demand_.generation);
  }
  if (changed)
    publish_status();
}

void ObsFrameCapture::on_status(std::function<void(CompanionStatusSnapshot)> handler)
{
  status_handler_ = std::move(handler);
  publish_status();
}

void ObsFrameCapture::receive_program_frame(void* context, video_data* frame)
{
  if (context && frame)
    static_cast<ObsFrameCapture*>(context)->on_program_frame(*frame);
}

void ObsFrameCapture::handle_frontend_event(obs_frontend_event event, void* context)
{
  if (!context)
    return;

  switch (event)
  {
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
  case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
    static_cast<ObsFrameCapture*>(context)->lifecycle_.begin_capture_reload();
    break;
  case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
  case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
  {
    auto* capture = static_cast<ObsFrameCapture*>(context);
    capture->lifecycle_.begin_capture_reload();
    capture->reload_completion_pending_ = true;
    break;
  }
  case OBS_FRONTEND_EVENT_FINISHED_LOADING:
  case OBS_FRONTEND_EVENT_SCENE_CHANGED:
  case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
  case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
  case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
    static_cast<ObsFrameCapture*>(context)->refresh_preview_source();
    break;
  case OBS_FRONTEND_EVENT_EXIT:
    // OBS destroys the frontend callback list after its EXIT notification and before module
    // unload, so a later removal would only log a warning.
    static_cast<ObsFrameCapture*>(context)->frontend_callback_registered_ = false;
    break;
  default:
    break;
  }
}

void ObsFrameCapture::on_program_frame(video_data& frame)
{
  if (!capture_active_.load(std::memory_order_acquire))
    return;
  const auto requested_fps = requested_program_fps_.load(std::memory_order_acquire);
  if (requested_fps == 0)
    return;
  if (!program_cadence_.should_publish(frame.timestamp, requested_fps))
    return;

  publisher_.publish(
      VideoTrack::program, frame.data[0], width_, height_, frame.linesize[0], frame.timestamp);
}

void ObsFrameCapture::on_preview_frame(video_data& frame)
{
  if (!capture_active_.load(std::memory_order_acquire))
    return;
  if (!preview_callback_logged_)
  {
    blog(LOG_INFO, "[PaneLive Companion] Preview view produced its first raw frame");
    preview_callback_logged_ = true;
  }
  const auto requested_fps = requested_preview_fps_.load(std::memory_order_acquire);
  if (requested_fps == 0)
    return;
  if (!preview_cadence_.should_publish(frame.timestamp, requested_fps))
    return;

  publisher_.publish(
      VideoTrack::preview, frame.data[0], width_, height_, frame.linesize[0], frame.timestamp);
}

void ObsFrameCapture::refresh_preview_source()
{
  if (lifecycle_.state() != CompanionCaptureState::starting &&
      lifecycle_.state() != CompanionCaptureState::active)
    return;
  obs_source_t* source = obs_frontend_preview_program_mode_active()
                             ? obs_frontend_get_current_preview_scene()
                             : obs_frontend_get_current_scene();
  if (source && !preview_view_ && !create_preview_view())
    blog(LOG_ERROR, "[PaneLive Companion] Could not create the headless Preview view");
  if (preview_view_)
  {
    // obs_view_create produces an AUX_VIEW. OBS therefore adds only showing references when this
    // source is attached. Replacing this with a MAIN_VIEW active reference, a temporary Program
    // scene change, or an active reference would start Preview-only Media Sources and change OBS
    // behavior merely because the Companion is observing them.
    obs_view_set_source(preview_view_, 0, source);
  }
  obs_source_release(source);
}

bool ObsFrameCapture::activate_capture()
{
  if (lifecycle_.consume_capture_generation_reset())
    publisher_.reset_capture_generation();
  program_cadence_ = {};
  preview_cadence_ = {};
  preview_callback_logged_ = false;
  obs_add_raw_video_callback(&program_conversion_, receive_program_frame, this);
  program_callback_registered_ = true;
  refresh_preview_source();
  if (!preview_view_)
  {
    obs_remove_raw_video_callback(receive_program_frame, this);
    program_callback_registered_ = false;
    return false;
  }
  capture_active_.store(true, std::memory_order_release);
  blog(LOG_INFO,
       "[PaneLive Companion] Capture active for demand generation %llu, clients %u, Program %u "
       "FPS, Preview %u FPS",
       static_cast<unsigned long long>(demand_.generation),
       demand_.demand.active_video_client_count,
       demand_.demand.requested_program_fps,
       demand_.demand.requested_preview_fps);
  return true;
}

void ObsFrameCapture::deactivate_capture()
{
  capture_active_.store(false, std::memory_order_release);
  requested_program_fps_.store(0, std::memory_order_release);
  requested_preview_fps_.store(0, std::memory_order_release);
  if (program_callback_registered_)
  {
    obs_remove_raw_video_callback(receive_program_frame, this);
    program_callback_registered_ = false;
  }
  destroy_preview_view();
  program_cadence_ = {};
  preview_cadence_ = {};
}

void ObsFrameCapture::publish_status()
{
  if (!status_handler_)
    return;
  CompanionStatusSnapshot snapshot;
  snapshot.generation = ++status_generation_;
  snapshot.active_video_client_count = demand_.demand.active_video_client_count;
  snapshot.capture_state = lifecycle_.state();
  snapshot.requested_program_fps = demand_.demand.requested_program_fps;
  snapshot.requested_preview_fps = demand_.demand.requested_preview_fps;
  snapshot.companion_heartbeat_fresh = demand_.heartbeat_fresh;
  switch (snapshot.capture_state)
  {
  case CompanionCaptureState::starting:
    snapshot.status_reason = "starting-output";
    break;
  case CompanionCaptureState::active:
    snapshot.status_reason = "capturing";
    break;
  case CompanionCaptureState::stopping:
    snapshot.status_reason = "stopping-output";
    break;
  case CompanionCaptureState::faulted:
    snapshot.status_reason = "output-start-failed";
    break;
  case CompanionCaptureState::idle:
    snapshot.status_reason = demand_.heartbeat_fresh ? "waiting-for-client" : "stale-companion";
    break;
  }
  status_handler_(std::move(snapshot));
}

bool ObsFrameCapture::create_preview_view()
{
  preview_view_ = obs_view_create();
  if (!preview_view_)
    return false;

  preview_video_ = obs_view_add(preview_view_);
  if (!preview_video_)
  {
    obs_view_destroy(preview_view_);
    preview_view_ = nullptr;
    return false;
  }

  if (preview_output_.start(preview_video_, preview_conversion_))
    return true;

  obs_view_remove(preview_view_);
  obs_view_destroy(preview_view_);
  preview_video_ = nullptr;
  preview_view_ = nullptr;
  return false;
}

void ObsFrameCapture::destroy_preview_view()
{
  if (!preview_view_)
    return;

  obs_view_set_source(preview_view_, 0, nullptr);
  preview_output_.stop();
  obs_view_remove(preview_view_);
  obs_view_destroy(preview_view_);
  preview_video_ = nullptr;
  preview_view_ = nullptr;
}

} // namespace studio_remote::obs_plugin
