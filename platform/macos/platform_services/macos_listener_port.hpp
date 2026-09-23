#pragma once

#include <studio_remote/discovery/listener_port.hpp>

namespace studio_remote::macos
{
/// Windows exclusive-bind preflight translated to a BSD wildcard bind without address reuse.
ListenerPortAvailability check_listener_port_availability(std::uint16_t port);
} // namespace studio_remote::macos
