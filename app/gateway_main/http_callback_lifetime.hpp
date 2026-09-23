#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>

namespace studio_remote
{
/// Gates callbacks borrowing the HTTP server. The lifecycle owner closes before destruction.
/// Callback bodies run without this gate's mutex, including callbacks entered under state locks.
class HttpCallbackLifetime final
{
public:
  class Lease final
  {
  public:
    Lease(Lease&& other) noexcept;
    ~Lease();
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

  private:
    friend class HttpCallbackLifetime;
    explicit Lease(HttpCallbackLifetime& owner);
    HttpCallbackLifetime* owner_;
  };

  void open();
  std::optional<Lease> enter();
  /// Called only by the lifecycle owner, never from an admitted callback.
  void close();

private:
  std::mutex mutex_;
  std::condition_variable idle_;
  bool accepting_ = false;
  std::size_t active_ = 0;
};
} // namespace studio_remote
