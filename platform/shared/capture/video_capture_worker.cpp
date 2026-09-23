#include "video_capture_worker.hpp"

#include <studio_remote/ipc/video_frame_reader.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace studio_remote
{
namespace
{

constexpr auto polling_interval = std::chrono::milliseconds(4);
constexpr auto reconnect_interval = std::chrono::milliseconds(250);
constexpr std::uint64_t unavailable_after_ns = 2'000'000'000ULL;

std::pair<double, double> sample_frame_content(const RawVideoFrame& frame)
{
  constexpr std::size_t maximum_samples = 4'096;
  const auto pixel_count = static_cast<std::size_t>(frame.width) * frame.height;
  const auto step = std::max<std::size_t>(1, pixel_count / maximum_samples);
  std::uint64_t channel_sum = 0;
  std::size_t sampled = 0;
  std::size_t non_black = 0;
  for (std::size_t pixel = 0; pixel < pixel_count; pixel += step)
  {
    const auto y = pixel / frame.width;
    const auto x = pixel % frame.width;
    const auto* bgra = frame.bgra.data() + y * frame.stride + x * ipc::bytes_per_pixel;
    const auto pixel_sum = static_cast<std::uint32_t>(bgra[0]) + bgra[1] + bgra[2];
    channel_sum += pixel_sum;
    non_black += pixel_sum > 12;
    ++sampled;
  }
  if (sampled == 0)
    return {};
  return {static_cast<double>(channel_sum) / (3.0 * static_cast<double>(sampled)),
          100.0 * static_cast<double>(non_black) / static_cast<double>(sampled)};
}

} // namespace

VideoCaptureWorker::VideoCaptureWorker(FrameFactory frames,
                                       DemandFactory demand,
                                       MonotonicClock clock,
                                       FailureReporter report_failure,
                                       TrackHandler track_handler,
                                       HealthHandler health_handler,
                                       DemandProvider demand_provider,
                                       GenerationHandler generation_handler)
    : frame_factory_(std::move(frames)), demand_factory_(std::move(demand)),
      clock_(std::move(clock)), report_failure_(report_failure),
      track_handler_(std::move(track_handler)), health_handler_(std::move(health_handler)),
      demand_provider_(std::move(demand_provider)),
      generation_handler_(std::move(generation_handler))
{
  if (!frame_factory_ || !demand_factory_ || !clock_ || !report_failure_)
    throw std::invalid_argument("Video capture worker requires IPC, clock and error reporting");
}

VideoCaptureWorker::~VideoCaptureWorker()
{
  stop();
}

void VideoCaptureWorker::start()
{
  std::lock_guard lock(lifecycle_mutex_);
  if (capture_thread_.joinable())
    return;
  capture_thread_ = std::jthread(
      [this](std::stop_token stop) noexcept
      {
        try
        {
          capture_loop(stop);
        }
        catch (...)
        {
          capture_failures_.fetch_add(1, std::memory_order_relaxed);
          report_failure_("OBS capture worker terminated unexpectedly");
          try
          {
            disconnect_mapping();
          }
          catch (...)
          {
            report_failure_("OBS capture worker could not clear stopped state");
          }
        }
      });
  capture_thread_id_ = capture_thread_.get_id();
}
void VideoCaptureWorker::stop()
{
  std::jthread stopping;
  {
    std::lock_guard lock(lifecycle_mutex_);
    if (capture_thread_id_ == std::this_thread::get_id())
      throw std::logic_error("Capture callbacks cannot stop their worker");
    stopping = std::move(capture_thread_);
  }
  if (stopping.joinable())
  {
    stopping.request_stop();
    wake_.notify_all();
    stopping.join();
    std::lock_guard lock(lifecycle_mutex_);
    capture_thread_id_ = {};
  }
  disconnect_mapping();
}
void VideoCaptureWorker::capture_loop(std::stop_token stop)
{
  bool failure_logged = false;
  while (!stop.stop_requested())
  {
    auto delay = polling_interval;
    try
    {
      bool notify_health = true;
      if (!frames_ && !connect_mapping())
        delay = reconnect_interval;
      else
      {
        notify_health = poll_capture();
        if (!frames_)
          delay = reconnect_interval;
      }
      if (notify_health)
      {
        publish_health("program", program_);
        publish_health("preview", preview_);
      }
    }
    catch (...)
    {
      capture_failures_.fetch_add(1, std::memory_order_relaxed);
      if (!failure_logged)
      {
        report_failure_("OBS capture worker failed; reconnecting helper resources");
        failure_logged = true;
      }
      disconnect_mapping();
      delay = reconnect_interval;
    }
    std::unique_lock wait_lock(wait_mutex_);
    wake_.wait_for(wait_lock, stop, delay, [] { return false; });
  }
  disconnect_mapping();
}
bool VideoCaptureWorker::poll_capture()
{
  const auto producer = frames_->producer();
  const auto observed_at = clock_();
  // This timestamp records frame publication, not an idle-process heartbeat. Allow initial
  // demand two seconds to produce frames; stale views are then reopened, including idle views.
  const bool grace_elapsed =
      observed_at >= connected_at_ns_ && observed_at - connected_at_ns_ > unavailable_after_ns;
  const bool stale_frames =
      producer && (producer->heartbeat_ns == 0 || producer->heartbeat_ns > observed_at ||
                   observed_at - producer->heartbeat_ns > unavailable_after_ns);
  if (!producer || producer->process_id != control_->producer_process_id() ||
      (grace_elapsed && stale_frames))
  {
    disconnect_mapping();
    return true;
  }
  if (!observe_capture_generation(*producer))
    return false;
  {
    std::lock_guard lock(mutex_);
    producer_pid_ = producer->process_id;
    producer_heartbeat_ns_ = producer->heartbeat_ns;
  }
  const auto now = clock_();
  VideoDemand demand = demand_provider_ ? demand_provider_() : VideoDemand{};
  demand.requested_program_fps =
      std::min(demand.requested_program_fps, ipc::maximum_requested_video_fps);
  demand.requested_preview_fps =
      std::min(demand.requested_preview_fps, ipc::maximum_requested_video_fps);
  if (demand.active_video_client_count == 0)
    demand = {};
  if (!update_video_demand(demand))
  {
    disconnect_mapping();
    return true;
  }
  if (!control_->publish(demand, now))
  {
    disconnect_mapping();
    return true;
  }
  const auto generation = control_->generation();
  {
    std::lock_guard lock(mutex_);
    demand_generation_ = generation;
  }
  if (demand.active_video_client_count != 0)
  {
    read_track("program", CapturedVideoTrack::program, program_);
    read_track("preview", CapturedVideoTrack::preview, preview_);
  }
  return true;
}

bool VideoCaptureWorker::update_video_demand(const VideoDemand& demand)
{
  bool activating;
  {
    std::lock_guard lock(mutex_);
    activating =
        current_demand_.active_video_client_count == 0 && demand.active_video_client_count != 0;
  }
  if (activating)
  {
    // Sample after demand evaluation. These resource calls must not hold the health mutex.
    const auto counts = frames_->published_frames();
    if (!counts)
      return false;
    program_.sequence_tracker.reset_identity();
    preview_.sequence_tracker.reset_identity();
    program_.activation_published_frames = counts->program;
    preview_.activation_published_frames = counts->preview;
  }
  std::lock_guard lock(mutex_);
  current_demand_ = demand;
  return true;
}

bool VideoCaptureWorker::observe_capture_generation(const VideoProducerSnapshot& producer)
{
  const auto program_generation = producer.program_generation;
  const auto preview_generation = producer.preview_generation;
  if (program_generation != preview_generation)
    return false;

  bool changed = false;
  {
    std::lock_guard lock(mutex_);
    if (!capture_generation_)
      capture_generation_ = program_generation;
    else if (*capture_generation_ != program_generation)
    {
      capture_generation_ = program_generation;
      program_.sequence_tracker.reset_identity();
      preview_.sequence_tracker.reset_identity();
      program_.first_frame_ns = 0;
      program_.last_frame_ns = 0;
      program_.sampled_average_channel = 0.0;
      program_.sampled_non_black_percent = 0.0;
      preview_.first_frame_ns = 0;
      preview_.last_frame_ns = 0;
      preview_.sampled_average_channel = 0.0;
      preview_.sampled_non_black_percent = 0.0;
      changed = true;
    }
  }
  if (changed && generation_handler_)
    generation_handler_(program_generation);
  return true;
}

bool VideoCaptureWorker::connect_mapping()
{
  auto frames = frame_factory_();
  if (!frames)
    return false;
  const auto producer = frames->producer();
  const auto now = clock_();
  if (!producer || producer->process_id == 0)
    return false;
  auto control = demand_factory_(producer->process_id);
  if (!control)
    return false;
  connected_at_ns_ = now;
  const auto demand_generation = control->generation();
  frames_ = std::move(frames);
  control_ = std::move(control);
  {
    std::lock_guard lock(mutex_);
    mapping_connected_ = true;
    producer_pid_ = producer->process_id;
    producer_heartbeat_ns_ = producer->heartbeat_ns;
    demand_generation_ = demand_generation;
    capture_generation_.reset();
    program_.sequence_tracker.reset_identity();
    preview_.sequence_tracker.reset_identity();
    program_.last_frame_ns = 0;
    preview_.last_frame_ns = 0;
  }
  return true;
}
void VideoCaptureWorker::disconnect_mapping()
{
  control_.reset();
  frames_.reset();
  std::lock_guard lock(mutex_);
  mapping_connected_ = false;
  producer_pid_ = 0;
  producer_heartbeat_ns_ = 0;
  current_demand_ = {};
}

void VideoCaptureWorker::read_track(std::string_view name,
                                    CapturedVideoTrack track,
                                    TrackState& state)
{
  // Sequencing/floor belong only to the capture thread. Copying and resource callbacks cannot
  // hold the health mutex: UI snapshots must remain available while a frame is acquired.
  auto result = frames_->read(track, state.sequence_tracker, state.activation_published_frames);
  auto frame = std::move(result.frame);
  const auto sample = frame ? sample_frame_content(*frame) : std::pair<double, double>{};
  const auto observed_at = frame ? clock_() : 0;
  {
    std::lock_guard lock(mutex_);
    state.dropped_frames += result.dropped_frames;
    if (!frame)
      return;
    state.sampled_average_channel = sample.first;
    state.sampled_non_black_percent = sample.second;
    state.last_frame_ns = observed_at;
    if (state.first_frame_ns == 0)
      state.first_frame_ns = state.last_frame_ns;
    ++state.received_frames;
  }
  track_handler_(name, std::move(frame));
}

void VideoCaptureWorker::publish_health(std::string_view name, TrackState& state)
{
  const std::uint64_t now = clock_();
  std::unique_lock state_lock(mutex_);
  const bool available = current_demand_.active_video_client_count != 0 &&
                         state.last_frame_ns != 0 &&
                         now - state.last_frame_ns < unavailable_after_ns;

  if (available == state.previously_available)
  {
    return;
  }
  state.previously_available = available;

  const json health = {
      {"available", available},
      {"backend", "obs-plugin-shared-memory"},
      {"capturedFrames", state.received_frames},
      {"drops", state.dropped_frames},
      {"sampledAverageChannel", state.sampled_average_channel},
      {"sampledNonBlackPercent", state.sampled_non_black_percent},
      {"ageMs",
       state.last_frame_ns ? json((now - state.last_frame_ns) / 1'000'000ULL) : json(nullptr)}};
  state_lock.unlock();

  if (health_handler_)
  {
    health_handler_(name, health);
  }
}

json VideoCaptureWorker::health() const
{
  const std::uint64_t now = clock_();
  std::lock_guard lock(mutex_);
  const auto track_health = [this, now](const TrackState& state)
  {
    const bool available = current_demand_.active_video_client_count != 0 &&
                           state.last_frame_ns != 0 &&
                           now - state.last_frame_ns < unavailable_after_ns;
    const auto elapsed_ns =
        state.first_frame_ns != 0 && now > state.first_frame_ns ? now - state.first_frame_ns : 0;
    const auto average_fps = elapsed_ns == 0
                                 ? 0.0
                                 : static_cast<double>(state.received_frames) * 1'000'000'000.0 /
                                       static_cast<double>(elapsed_ns);
    return json{
        {"available", available},
        {"backend", "obs-plugin-shared-memory"},
        {"capturedFrames", state.received_frames},
        {"drops", state.dropped_frames},
        {"averageFps", average_fps},
        {"sampledAverageChannel", state.sampled_average_channel},
        {"sampledNonBlackPercent", state.sampled_non_black_percent},
        {"ageMs",
         state.last_frame_ns ? json((now - state.last_frame_ns) / 1'000'000ULL) : json(nullptr)}};
  };
  return {{"ipcConnected", mapping_connected_},
          {"producerPid", producer_pid_},
          {"producerHeartbeatNs", producer_heartbeat_ns_},
          {"activeVideoClientCount", current_demand_.active_video_client_count},
          {"demandGeneration", demand_generation_},
          {"captureGeneration", capture_generation_ ? json(*capture_generation_) : json(nullptr)},
          {"captureFailures", capture_failures_.load(std::memory_order_relaxed)},
          {"program", track_health(program_)},
          {"preview", track_health(preview_)}};
}

} // namespace studio_remote
