#pragma once

#include <atomic>
#include <functional>

struct ssl_ctx_st;

namespace studio_remote
{
/// Native transport/composition boundary. Captures must keep the identity alive until listener
/// stop.
using HttpTlsInitializer = std::function<void(ssl_ctx_st&)>;

/// Contains exceptions at CivetWeb's C callback boundary. Configuration is immutable after
/// creation.
class HttpTlsContext
{
public:
  explicit HttpTlsContext(HttpTlsInitializer initializer);
  int initialize(void* context) noexcept;
  bool failed() const noexcept;

private:
  const HttpTlsInitializer initializer_;
  std::atomic<bool> failed_{false};
};
} // namespace studio_remote
