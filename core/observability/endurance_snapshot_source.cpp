#include <studio_remote/observability/endurance_snapshot_source.hpp>

#include <stdexcept>
#include <utility>

namespace studio_remote
{
class EnduranceSnapshotSource::Invocation final
{
public:
  explicit Invocation(EnduranceSnapshotSource& owner) : owner_(owner)
  {
  }
  ~Invocation()
  {
    owner_.finish_sample();
  }
  Invocation(const Invocation&) = delete;
  Invocation& operator=(const Invocation&) = delete;

private:
  EnduranceSnapshotSource& owner_;
};

EnduranceSnapshotSource::~EnduranceSnapshotSource()
{
  replace({});
}

void EnduranceSnapshotSource::replace(Provider provider)
{
  // Check before waiting for another replacement: that caller may already be waiting for this
  // very callback to return. Taking replacement_mutex_ first would make self-replacement deadlock.
  {
    std::lock_guard lock(mutex_);
    if (active_thread_ == std::this_thread::get_id())
      throw std::logic_error("An endurance snapshot cannot replace its own provider");
  }
  Provider previous;
  {
    std::lock_guard replacement(replacement_mutex_);
    std::unique_lock lock(mutex_);
    replacing_ = true;
    idle_.wait(lock, [this] { return active_thread_ == std::thread::id{}; });
    previous = std::move(provider_);
    provider_ = std::move(provider);
    replacing_ = false;
  }
}

std::optional<json> EnduranceSnapshotSource::sample()
{
  {
    std::lock_guard lock(mutex_);
    if (replacing_ || !provider_)
      return std::nullopt;
    if (active_thread_ != std::thread::id{})
      throw std::logic_error("An endurance snapshot is already being collected");
    active_thread_ = std::this_thread::get_id();
  }
  Invocation invocation(*this);
  return provider_();
}

void EnduranceSnapshotSource::finish_sample()
{
  std::lock_guard lock(mutex_);
  active_thread_ = {};
  idle_.notify_all();
}
} // namespace studio_remote
