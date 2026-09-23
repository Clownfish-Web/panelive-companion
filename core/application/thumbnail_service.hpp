#pragma once

#include <studio_remote/media/thumbnail.hpp>
#include <studio_remote/state/state_store.hpp>

#include <condition_variable>
#include <deque>
#include <future>
#include <list>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace studio_remote
{

enum class ThumbnailResultStatus
{
  image,
  not_modified,
  resource_not_found,
  stale_state,
  unsupported_source,
  queue_full,
  unavailable
};

struct ThumbnailResult
{
  ThumbnailResultStatus status = ThumbnailResultStatus::unavailable;
  std::string reason;
  std::string content_type;
  std::string etag;
  std::uint64_t observed_at_ns = 0;
  bool stale = false;
  bool placeholder = false;
  std::vector<std::uint8_t> bytes;
};

struct ThumbnailServiceLimits
{
  std::chrono::milliseconds freshness = std::chrono::seconds(5);
  std::chrono::milliseconds failure_retry_interval = std::chrono::seconds(10);
  std::chrono::milliseconds cold_request_timeout = std::chrono::seconds(4);
  std::size_t maximum_queue_size = 16;
  std::size_t maximum_cache_entries = 256;
  std::size_t maximum_cache_bytes = 32U * 1024U * 1024U;
};

class ThumbnailService
{
public:
  ThumbnailService(StateStore& state,
                   IThumbnailCapture& capture,
                   IThumbnailEncoder& encoder,
                   ThumbnailServiceLimits limits = {});
  ~ThumbnailService();

  ThumbnailService(const ThumbnailService&) = delete;
  ThumbnailService& operator=(const ThumbnailService&) = delete;

  ThumbnailResult get_source(std::string_view source_id,
                             std::uint32_t maximum_width,
                             std::uint32_t maximum_height,
                             std::string_view if_none_match = {});
  ThumbnailResult get_scene(std::string_view scene_id,
                            std::uint32_t maximum_width,
                            std::uint32_t maximum_height,
                            std::string_view if_none_match = {});
  void invalidate();
  bool available() const noexcept;
  json diagnostics() const;

private:
  struct Key
  {
    ThumbnailTarget target = ThumbnailTarget::source;
    std::string resource_id;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t catalog_generation = 0;
    std::uint64_t service_generation = 0;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash
  {
    std::size_t operator()(const Key& key) const noexcept;
  };
  struct CacheEntry
  {
    ThumbnailResult image;
    Clock::time_point captured_at;
    std::list<Key>::iterator lru;
  };
  struct FailureEntry
  {
    ThumbnailResult result;
    Clock::time_point failed_at;
  };
  struct Task
  {
    Key key;
    std::string resource_name;
    std::shared_ptr<std::promise<ThumbnailResult>> completion;
  };

  ThumbnailResult get(ThumbnailTarget target,
                      std::string resource_id,
                      std::string resource_name,
                      std::uint64_t catalog_generation,
                      std::uint32_t maximum_width,
                      std::uint32_t maximum_height,
                      std::string_view if_none_match);
  std::shared_future<ThumbnailResult>
  enqueue_locked(const Key& key, std::string_view resource_name, bool& queue_full);
  void worker_loop();
  ThumbnailResult capture(const Task& task);
  ThumbnailResult capture_or_placeholder(const Task& task);
  void store_locked(const Key& key, ThumbnailResult image);
  void store_failure_locked(const Key& key, ThumbnailResult failure);
  void event_locked(std::string name, const Key& key, json detail = {});
  static ThumbnailResult conditional(ThumbnailResult result, std::string_view if_none_match);

  StateStore& state_;
  IThumbnailCapture& capture_;
  IThumbnailEncoder& encoder_;
  ThumbnailServiceLimits limits_;
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  bool stopping_ = false;
  std::thread worker_;
  std::deque<Task> queue_;
  std::unordered_map<Key, std::shared_future<ThumbnailResult>, KeyHash> pending_;
  std::unordered_map<Key, CacheEntry, KeyHash> cache_;
  std::unordered_map<Key, FailureEntry, KeyHash> failure_cache_;
  std::list<Key> lru_;
  std::size_t cache_bytes_ = 0;
  std::uint64_t generation_ = 1;
  std::optional<Clock::time_point> last_activity_at_;
  std::deque<json> events_;
};

} // namespace studio_remote
