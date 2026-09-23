#pragma once

#include <studio_remote/common/types.hpp>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace studio_remote
{
/// One recorder worker samples; lifecycle callers replace or clear the provider. Clearing waits
/// for a sample to finish before releasing borrowed dependencies. Callbacks must not replace,
/// destroy or recursively sample their own source. Callback execution/destruction holds no lock.
class EnduranceSnapshotSource final
{
public:
  using Provider = std::function<json()>;

  EnduranceSnapshotSource() = default;
  ~EnduranceSnapshotSource();
  EnduranceSnapshotSource(const EnduranceSnapshotSource&) = delete;
  EnduranceSnapshotSource& operator=(const EnduranceSnapshotSource&) = delete;

  void replace(Provider provider);
  std::optional<json> sample();

private:
  class Invocation;
  void finish_sample();

  std::mutex replacement_mutex_;
  std::mutex mutex_;
  std::condition_variable idle_;
  Provider provider_;
  std::thread::id active_thread_;
  bool replacing_ = false;
};
} // namespace studio_remote
