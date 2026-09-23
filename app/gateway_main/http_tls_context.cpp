#include "http_tls_context.hpp"

#include <utility>

namespace studio_remote
{
HttpTlsContext::HttpTlsContext(HttpTlsInitializer initializer)
    : initializer_(std::move(initializer))
{
}

int HttpTlsContext::initialize(void* context) noexcept
{
  if (!initializer_)
    return 0;
  try
  {
    if (context)
    {
      initializer_(*static_cast<ssl_ctx_st*>(context));
      return 1;
    }
  }
  catch (...)
  {
    // Startup observes this flag and reports a fixed error without leaking identity material.
  }
  failed_.store(true);
  return -1;
}

bool HttpTlsContext::failed() const noexcept
{
  return failed_.load();
}
} // namespace studio_remote
