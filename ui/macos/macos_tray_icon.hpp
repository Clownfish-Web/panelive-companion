#pragma once

#include <studio_remote/ui/management_interface.hpp>

#include <functional>
#include <memory>

namespace studio_remote::macos
{
struct MacosTrayActions
{
  std::function<void()> open_management;
  std::function<void()> pair_client;
  std::function<void(ManagementInterfaceMode)> set_interface_mode;
  std::function<ManagementInterfaceMode()> interface_mode;
  std::function<void()> shutdown;
};

/// Main-thread notification resource, corresponding to WindowsTrayIcon. Its owner removes it
/// before destroying the window/controller referenced by its actions. It owns no backend state.
class MacosTrayIcon final
{
public:
  explicit MacosTrayIcon(MacosTrayActions actions);
  ~MacosTrayIcon();
  MacosTrayIcon(const MacosTrayIcon&) = delete;
  MacosTrayIcon& operator=(const MacosTrayIcon&) = delete;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
