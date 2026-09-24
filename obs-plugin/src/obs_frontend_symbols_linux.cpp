#include "obs_frontend_symbols.hpp"

#include <dlfcn.h>

namespace studio_remote::obs_plugin
{
void* obs_frontend_symbol(const char* name) noexcept
{
  return dlsym(RTLD_DEFAULT, name);
}
} // namespace studio_remote::obs_plugin
