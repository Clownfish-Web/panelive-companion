#pragma once

#include <studio_remote/ui/management_controller.hpp>

#include <string>
#include <vector>

namespace studio_remote::linux
{
struct LinuxDesktopStatus
{
  std::string title = "PaneLive Companion";
  std::vector<std::string> lines;
  bool attention = false;

  bool operator==(const LinuxDesktopStatus&) const = default;
};

/// Presentation-only projection of the existing authenticated management observations.
LinuxDesktopStatus make_linux_desktop_status(const ManagementUiState& state);
} // namespace studio_remote::linux
