#include "obs_frontend_symbols.hpp"

#include <Windows.h>
#include <cstring>

namespace studio_remote::obs_plugin
{
void* obs_frontend_symbol(const char* name) noexcept
{
  HMODULE module = GetModuleHandleW(L"obs-frontend-api.dll");
  if (!module)
    module = GetModuleHandleW(nullptr);
  const auto address = module ? GetProcAddress(module, name) : nullptr;
  void* result = nullptr;
  static_assert(sizeof(result) == sizeof(address));
  std::memcpy(&result, &address, sizeof(result));
  return result;
}
} // namespace studio_remote::obs_plugin
