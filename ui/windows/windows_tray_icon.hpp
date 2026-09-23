#pragma once

#include <studio_remote/ui/management_interface.hpp>

#include <Windows.h>

#include <functional>

namespace studio_remote::windows
{
struct WindowsTrayActions
{
  std::function<void()> open_management;
  std::function<void()> pair_client;
  std::function<void(ManagementInterfaceMode)> set_interface_mode;
  std::function<ManagementInterfaceMode()> interface_mode;
  std::function<void()> shutdown;
};

class WindowsTrayIcon
{
public:
  explicit WindowsTrayIcon(WindowsTrayActions actions);
  ~WindowsTrayIcon();

  WindowsTrayIcon(const WindowsTrayIcon&) = delete;
  WindowsTrayIcon& operator=(const WindowsTrayIcon&) = delete;

private:
  static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
  void add_icon();
  void remove_icon();
  void show_menu();

  WindowsTrayActions actions_;
  HWND window_ = nullptr;
  UINT taskbar_created_message_ = 0;
  bool icon_added_ = false;
};
} // namespace studio_remote::windows
