#pragma once

#include <chrono>
#include <functional>
#include <stop_token>
#include <string_view>

namespace studio_remote::macos
{
/// Explicit startup import only, after instance ownership. The caller exclusively owns reading
/// this descriptor until return; this function does not close it. Disables/restores terminal
/// echo, bounds input to 4096 bytes, and cleanses its buffer. No credential is changed on failed,
/// cancelled, or timed-out input. Normal operation does not read a console. The borrowed callback
/// runs synchronously after echo restoration; its password view is valid only for that call.
void import_startup_obs_password(const std::function<void(std::string_view)>& persist,
                                 int descriptor,
                                 std::stop_token stop = {},
                                 std::chrono::milliseconds timeout = std::chrono::seconds(60));
} // namespace studio_remote::macos
