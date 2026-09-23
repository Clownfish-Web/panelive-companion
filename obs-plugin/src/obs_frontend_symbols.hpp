#pragma once

namespace studio_remote::obs_plugin
{
// Optional frontend APIs are resolved at runtime to preserve the helper's OBS compatibility floor.
void* obs_frontend_symbol(const char* name) noexcept;
} // namespace studio_remote::obs_plugin
