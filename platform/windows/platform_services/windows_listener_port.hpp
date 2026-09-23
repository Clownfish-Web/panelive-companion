#pragma once

#include <studio_remote/discovery/listener_port.hpp>

namespace studio_remote::windows
{
using studio_remote::ListenerPortAvailability;

/// Checks whether an exclusive TCP listener can bind the requested Companion port.
ListenerPortAvailability check_listener_port_availability(std::uint16_t port);
} // namespace studio_remote::windows
