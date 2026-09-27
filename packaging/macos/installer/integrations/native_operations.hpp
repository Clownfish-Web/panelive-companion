#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace studio_remote::macos::installer
{
/// Bounded synchronous native tools, owned and reaped before returning or throwing.
int run_tool(const std::string& executable, const std::vector<std::string>& arguments);
void verify_bundle(const std::filesystem::path& bundle);
void require_obs_closed();
void validate_shared_plugin_directory();
void ensure_shared_plugin_directory();
void authorize_integration(const std::filesystem::path& executable);
} // namespace studio_remote::macos::installer
