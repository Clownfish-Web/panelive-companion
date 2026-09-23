#pragma once

#include <CivetServer.h>

namespace studio_remote
{

// Startup callbacks receive the wrapper pointer before its mg_context is available.
inline const void* civet_callback_user_context(void* user_data) noexcept
{
  const auto* server = static_cast<const CivetServer*>(user_data);
  return server ? server->getUserContext() : nullptr;
}

// CivetWeb's C context stores the CivetServer wrapper, not the application context supplied to the
// C++ constructor. Native callbacks must cross both ownership layers before using application data.
inline const void* civet_user_context(const mg_context* context) noexcept
{
  const auto* server =
      context ? static_cast<const CivetServer*>(mg_get_user_data(context)) : nullptr;
  return server ? server->getUserContext() : nullptr;
}

} // namespace studio_remote
