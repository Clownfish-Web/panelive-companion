#pragma once

namespace studio_remote::macos
{
struct CommandLine;

/// Installer-only composition. Exits before credentials, providers, listeners or GUI startup.
int run_installer_operation(const CommandLine& command_line);
} // namespace studio_remote::macos
