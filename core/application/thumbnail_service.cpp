#include "thumbnail_service.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace studio_remote
{
namespace
{
std::string etag_for(const EncodedThumbnail& image, std::uint32_t width, std::uint32_t height)
{
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : image.bytes)
  {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  char value[80]{};
  std::snprintf(value,
                sizeof(value),
                "\"thumbnail-%08x-%08x-%016llx\"",
                width,
                height,
                static_cast<unsigned long long>(hash));
  return value;
}

RawThumbnail unavailable_placeholder(std::uint32_t maximum_width, std::uint32_t maximum_height)
{
  constexpr std::uint32_t aspect_width = 16;
  constexpr std::uint32_t aspect_height = 9;
  auto width = std::min(maximum_width, maximum_height * aspect_width / aspect_height);
  auto height = width * aspect_height / aspect_width;
  if (height == 0)
    height = 1;

  constexpr std::uint8_t background = 32;
  constexpr std::uint8_t red_blue = 48;
  constexpr std::uint8_t red_green = 58;
  constexpr std::uint8_t red_red = 224;
  const auto stride = width * 4;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * height, background);
  const auto center_x = static_cast<double>(width - 1) / 2.0;
  const auto center_y = static_cast<double>(height - 1) / 2.0;
  const auto radius = static_cast<double>(std::min(width, height)) * 0.28;
  const auto thickness = std::max(2.0, radius * 0.13);
  for (std::uint32_t y = 0; y < height; ++y)
  {
    for (std::uint32_t x = 0; x < width; ++x)
    {
      const auto offset_x = static_cast<double>(x) - center_x;
      const auto offset_y = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(offset_x * offset_x + offset_y * offset_y);
      const bool ring = std::abs(distance - radius) <= thickness;
      const bool slash = distance <= radius && std::abs(offset_x + offset_y) <= thickness;
      const auto offset = static_cast<std::size_t>(y) * stride + x * 4;
      pixels[offset + 3] = 255;
      if (!ring && !slash)
        continue;
      pixels[offset] = red_blue;
      pixels[offset + 1] = red_green;
      pixels[offset + 2] = red_red;
      pixels[offset + 3] = 255;
    }
  }
  return {width, height, stride, 0, std::move(pixels)};
}

ThumbnailResult failure_result(ThumbnailResultStatus status, std::string reason)
{
  ThumbnailResult result;
  result.status = status;
  result.reason = std::move(reason);
  return result;
}
} // namespace

std::size_t ThumbnailService::KeyHash::operator()(const Key& key) const noexcept
{
  auto value = std::hash<std::string>{}(key.resource_id);
  value ^= static_cast<std::size_t>(key.target) << 24U;
  value ^= static_cast<std::size_t>(key.width) << 1U;
  value ^= static_cast<std::size_t>(key.height) << 10U;
  value ^= static_cast<std::size_t>(key.catalog_generation);
  value ^= static_cast<std::size_t>(key.service_generation << 1U);
  return value;
}

ThumbnailService::ThumbnailService(StateStore& state,
                                   IThumbnailCapture& capture,
                                   IThumbnailEncoder& encoder,
                                   ThumbnailServiceLimits limits)
    : state_(state), capture_(capture), encoder_(encoder), limits_(limits)
{
  // Start only after every member used by the worker has been constructed.
  worker_ = std::thread([this] { worker_loop(); });
}

ThumbnailService::~ThumbnailService()
{
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  ready_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

ThumbnailResult ThumbnailService::get_source(std::string_view source_id,
                                             std::uint32_t maximum_width,
                                             std::uint32_t maximum_height,
                                             std::string_view if_none_match)
{
  if (!state_.fresh())
    return failure_result(ThumbnailResultStatus::stale_state, "state_not_fresh");
  const auto source = state_.source(source_id);
  if (!source)
    return failure_result(ThumbnailResultStatus::resource_not_found, "source_not_found");
  if (source->group)
    return failure_result(ThumbnailResultStatus::unsupported_source, "source_not_renderable");
  return get(ThumbnailTarget::source,
             source->id,
             source->name,
             source->catalog_generation,
             maximum_width,
             maximum_height,
             if_none_match);
}

ThumbnailResult ThumbnailService::get_scene(std::string_view scene_id,
                                            std::uint32_t maximum_width,
                                            std::uint32_t maximum_height,
                                            std::string_view if_none_match)
{
  if (!state_.fresh())
    return failure_result(ThumbnailResultStatus::stale_state, "state_not_fresh");
  const auto scene = state_.scene(scene_id);
  if (!scene)
    return failure_result(ThumbnailResultStatus::resource_not_found, "scene_not_found");
  return get(ThumbnailTarget::scene,
             scene->id,
             scene->name,
             scene->item_revision,
             maximum_width,
             maximum_height,
             if_none_match);
}

ThumbnailResult ThumbnailService::get(ThumbnailTarget target,
                                      std::string resource_id,
                                      std::string resource_name,
                                      std::uint64_t catalog_generation,
                                      std::uint32_t maximum_width,
                                      std::uint32_t maximum_height,
                                      std::string_view if_none_match)
{

  std::shared_future<ThumbnailResult> future;
  {
    std::lock_guard lock(mutex_);
    Key key{target,
            std::move(resource_id),
            maximum_width,
            maximum_height,
            catalog_generation,
            generation_};
    auto failure = failure_cache_.find(key);
    bool retry_suppressed = false;
    if (failure != failure_cache_.end())
    {
      retry_suppressed = Clock::now() - failure->second.failed_at < limits_.failure_retry_interval;
      if (!retry_suppressed)
        failure = failure_cache_.erase(failure);
    }
    const auto cached = cache_.find(key);
    if (cached != cache_.end())
    {
      lru_.splice(lru_.begin(), lru_, cached->second.lru);
      const auto age = Clock::now() - cached->second.captured_at;
      if (age < limits_.freshness)
      {
        event_locked(
            "thumbnail_cache_hit",
            key,
            {{"cacheAgeMs", std::chrono::duration_cast<std::chrono::milliseconds>(age).count()}});
        return conditional(cached->second.image, if_none_match);
      }
      if (!retry_suppressed)
      {
        bool ignored = false;
        enqueue_locked(key, resource_name, ignored);
      }
      auto stale = cached->second.image;
      stale.stale = true;
      event_locked(
          "thumbnail_served_stale",
          key,
          {{"cacheAgeMs", std::chrono::duration_cast<std::chrono::milliseconds>(age).count()}});
      return conditional(std::move(stale), if_none_match);
    }
    if (retry_suppressed)
    {
      event_locked(
          "thumbnail_failure_cache_hit", key, {{"failureCode", failure->second.result.reason}});
      return failure->second.result;
    }
    event_locked("thumbnail_cache_miss", key);
    bool queue_full = false;
    future = enqueue_locked(key, resource_name, queue_full);
    if (queue_full)
      return failure_result(ThumbnailResultStatus::queue_full, "capture_queue_full");
  }
  if (future.wait_for(limits_.cold_request_timeout) != std::future_status::ready)
    return failure_result(ThumbnailResultStatus::unavailable, "capture_pending");
  return conditional(future.get(), if_none_match);
}

std::shared_future<ThumbnailResult>
ThumbnailService::enqueue_locked(const Key& key, std::string_view resource_name, bool& queue_full)
{
  if (const auto existing = pending_.find(key); existing != pending_.end())
  {
    event_locked("thumbnail_request_coalesced", key);
    return existing->second;
  }
  if (queue_.size() >= limits_.maximum_queue_size)
  {
    queue_full = true;
    event_locked("thumbnail_rate_limited", key, {{"failureCode", "capture_queue_full"}});
    return {};
  }
  auto completion = std::make_shared<std::promise<ThumbnailResult>>();
  auto future = completion->get_future().share();
  pending_.emplace(key, future);
  queue_.push_back({key, std::string(resource_name), std::move(completion)});
  ready_.notify_one();
  return future;
}

void ThumbnailService::worker_loop()
{
  for (;;)
  {
    Task task;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty())
        return;
      task = std::move(queue_.front());
      queue_.pop_front();
      event_locked("thumbnail_capture_started", task.key);
    }
    const auto started = Clock::now();
    auto result = capture(task);
    const auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    {
      std::lock_guard lock(mutex_);
      if (task.key.service_generation != generation_)
        result = failure_result(ThumbnailResultStatus::stale_state, "mapping_invalidated");
      if (result.status == ThumbnailResultStatus::image &&
          task.key.service_generation == generation_)
      {
        store_locked(task.key, result);
        event_locked("thumbnail_capture_completed",
                     task.key,
                     {{"byteCount", result.bytes.size()}, {"captureDurationMs", duration}});
      }
      else
      {
        if (task.key.service_generation == generation_ &&
            result.status == ThumbnailResultStatus::unavailable)
          store_failure_locked(task.key, result);
        event_locked("thumbnail_capture_failed",
                     task.key,
                     {{"failureCode", result.reason}, {"captureDurationMs", duration}});
      }
      pending_.erase(task.key);
    }
    task.completion->set_value(std::move(result));
  }
}

ThumbnailResult ThumbnailService::capture(const Task& task)
{
  // Placeholder construction/encoding runs inside the typed capture-error handler. Keep the
  // ordinary failure boundary outside that handler so its exceptions also complete this task.
  try
  {
    return capture_or_placeholder(task);
  }
  catch (const std::exception&)
  {
    return failure_result(ThumbnailResultStatus::unavailable,
                          task.key.target == ThumbnailTarget::scene ? "scene_not_renderable"
                                                                    : "source_not_renderable");
  }
}

ThumbnailResult ThumbnailService::capture_or_placeholder(const Task& task)
{
  try
  {
    auto raw = capture_.capture({task.key.target,
                                 task.key.resource_id,
                                 task.resource_name,
                                 task.key.width,
                                 task.key.height});
    auto encoded = encoder_.encode(raw);
    ThumbnailResult result;
    result.status = ThumbnailResultStatus::image;
    result.content_type = std::move(encoded.content_type);
    result.etag = etag_for(encoded, task.key.width, task.key.height);
    result.observed_at_ns = raw.observed_at_ns;
    result.bytes = std::move(encoded.bytes);
    return result;
  }
  catch (const ThumbnailCaptureError& error)
  {
    if (task.key.target == ThumbnailTarget::source && error.code() == "source_unavailable")
    {
      auto placeholder = encoder_.encode(unavailable_placeholder(task.key.width, task.key.height));
      ThumbnailResult result;
      result.status = ThumbnailResultStatus::image;
      result.reason = error.code();
      result.content_type = std::move(placeholder.content_type);
      result.etag = etag_for(placeholder, task.key.width, task.key.height);
      result.placeholder = true;
      result.bytes = std::move(placeholder.bytes);
      return result;
    }
    return failure_result(ThumbnailResultStatus::unavailable, error.code());
  }
}

void ThumbnailService::store_locked(const Key& key, ThumbnailResult image)
{
  for (auto existing = failure_cache_.begin(); existing != failure_cache_.end();)
  {
    if (existing->first.target == key.target && existing->first.resource_id == key.resource_id)
      existing = failure_cache_.erase(existing);
    else
      ++existing;
  }
  for (auto existing = cache_.begin(); existing != cache_.end();)
  {
    if (existing->first.target == key.target && existing->first.resource_id == key.resource_id)
    {
      cache_bytes_ -= existing->second.image.bytes.size();
      lru_.erase(existing->second.lru);
      existing = cache_.erase(existing);
    }
    else
    {
      ++existing;
    }
  }
  lru_.push_front(key);
  cache_bytes_ += image.bytes.size();
  cache_.emplace(key, CacheEntry{std::move(image), Clock::now(), lru_.begin()});
  while (cache_.size() > limits_.maximum_cache_entries ||
         cache_bytes_ > limits_.maximum_cache_bytes)
  {
    const auto key_to_remove = lru_.back();
    if (const auto found = cache_.find(key_to_remove); found != cache_.end())
    {
      cache_bytes_ -= found->second.image.bytes.size();
      cache_.erase(found);
    }
    lru_.pop_back();
  }
}

void ThumbnailService::store_failure_locked(const Key& key, ThumbnailResult failure)
{
  failure_cache_.insert_or_assign(key, FailureEntry{std::move(failure), Clock::now()});
  while (failure_cache_.size() > limits_.maximum_cache_entries)
    failure_cache_.erase(failure_cache_.begin());
}

void ThumbnailService::invalidate()
{
  std::lock_guard lock(mutex_);
  ++generation_;
  cache_.clear();
  failure_cache_.clear();
  lru_.clear();
  cache_bytes_ = 0;
}

bool ThumbnailService::available() const noexcept
{
  return capture_.available();
}

json ThumbnailService::diagnostics() const
{
  std::lock_guard lock(mutex_);
  json result = {{"available", capture_.available()},
                 {"cacheEntries", cache_.size()},
                 {"cacheBytes", cache_bytes_},
                 {"negativeCacheEntries", failure_cache_.size()},
                 {"queuedCaptures", queue_.size()},
                 {"pendingCaptures", pending_.size()},
                 {"recentEvents", events_}};
  if (last_activity_at_)
  {
    result["lastActivityAgeMs"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *last_activity_at_)
            .count();
  }
  else
  {
    result["lastActivityAgeMs"] = nullptr;
  }
  return result;
}

void ThumbnailService::event_locked(std::string name, const Key& key, json detail)
{
  last_activity_at_ = Clock::now();
  detail["event"] = std::move(name);
  detail[key.target == ThumbnailTarget::scene ? "sceneId" : "sourceId"] =
      key.resource_id.substr(0, 8);
  detail["maxWidth"] = key.width;
  detail["maxHeight"] = key.height;
  events_.push_back(std::move(detail));
  while (events_.size() > 64)
    events_.pop_front();
}

ThumbnailResult ThumbnailService::conditional(ThumbnailResult result,
                                              std::string_view if_none_match)
{
  if (result.status == ThumbnailResultStatus::image && !if_none_match.empty() &&
      if_none_match == result.etag)
  {
    result.status = ThumbnailResultStatus::not_modified;
    result.bytes.clear();
  }
  return result;
}

} // namespace studio_remote
