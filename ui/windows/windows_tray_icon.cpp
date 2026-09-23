#include "windows_tray_icon.hpp"

#include "../../platform/windows/resources/windows_resource_ids.h"

#include <shellapi.h>

#include <winrt/base.h>

#include <memory>
#include <utility>

namespace studio_remote::windows
{
namespace
{
constexpr wchar_t tray_window_class[] = L"pl.clownfishweb.panelive.CompanionTray";
constexpr UINT tray_callback_message = WM_APP + 51;
constexpr UINT tray_icon_id = 1;
constexpr UINT open_command = 1001;
constexpr UINT pair_command = 1002;
constexpr UINT basic_command = 1003;
constexpr UINT advanced_command = 1004;
constexpr UINT shutdown_command = 1005;

struct MenuDeleter
{
  void operator()(HMENU menu) const noexcept
  {
    if (!DestroyMenu(menu))
      OutputDebugStringW(L"Could not destroy the companion tray menu.\n");
  }
};

HICON load_icon()
{
  return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                       MAKEINTRESOURCEW(IDI_PANELIVE_COMPANION),
                                       IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON),
                                       GetSystemMetrics(SM_CYSMICON),
                                       LR_DEFAULTCOLOR | LR_SHARED));
}
} // namespace

WindowsTrayIcon::WindowsTrayIcon(WindowsTrayActions actions) : actions_(std::move(actions))
{
  WNDCLASSEXW window_class{sizeof(window_class)};
  window_class.lpfnWndProc = window_proc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = tray_window_class;
  RegisterClassExW(&window_class);
  window_ = CreateWindowExW(0,
                            tray_window_class,
                            L"PaneLive Companion",
                            0,
                            0,
                            0,
                            0,
                            0,
                            HWND_MESSAGE,
                            nullptr,
                            GetModuleHandleW(nullptr),
                            this);
  if (!window_)
    winrt::throw_last_error();
  taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
  add_icon();
}

WindowsTrayIcon::~WindowsTrayIcon()
{
  remove_icon();
  if (window_)
    DestroyWindow(window_);
}

LRESULT CALLBACK WindowsTrayIcon::window_proc(HWND window,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam)
{
  auto* self = reinterpret_cast<WindowsTrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE)
  {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    self = static_cast<WindowsTrayIcon*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self)
    return DefWindowProcW(window, message, wparam, lparam);
  try
  {
    return self->handle_message(window, message, wparam, lparam);
  }
  catch (...)
  {
    OutputDebugStringW(L"Companion tray action failed.\n");
    return 0;
  }
}

LRESULT WindowsTrayIcon::handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
  if (message == taskbar_created_message_)
  {
    add_icon();
    return 0;
  }
  if (message == tray_callback_message)
  {
    if (LOWORD(lparam) == WM_LBUTTONUP || LOWORD(lparam) == NIN_SELECT)
      actions_.open_management();
    else if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP)
      show_menu();
    return 0;
  }
  if (message == WM_COMMAND)
  {
    switch (LOWORD(wparam))
    {
    case open_command:
      actions_.open_management();
      break;
    case pair_command:
      actions_.pair_client();
      break;
    case basic_command:
      actions_.set_interface_mode(ManagementInterfaceMode::basic);
      break;
    case advanced_command:
      actions_.set_interface_mode(ManagementInterfaceMode::advanced);
      break;
    case shutdown_command:
      actions_.shutdown();
      break;
    }
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void WindowsTrayIcon::add_icon()
{
  NOTIFYICONDATAW icon{sizeof(icon)};
  icon.hWnd = window_;
  icon.uID = tray_icon_id;
  icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  icon.uCallbackMessage = tray_callback_message;
  icon.hIcon = load_icon();
  wcscpy_s(icon.szTip, L"PaneLive Companion");
  icon_added_ = Shell_NotifyIconW(icon_added_ ? NIM_MODIFY : NIM_ADD, &icon) != FALSE;
  if (icon_added_)
  {
    icon.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon);
  }
}

void WindowsTrayIcon::remove_icon()
{
  if (!icon_added_)
    return;
  NOTIFYICONDATAW icon{sizeof(icon)};
  icon.hWnd = window_;
  icon.uID = tray_icon_id;
  Shell_NotifyIconW(NIM_DELETE, &icon);
  icon_added_ = false;
}

void WindowsTrayIcon::show_menu()
{
  const HMENU menu = CreatePopupMenu();
  if (!menu)
    winrt::throw_last_error();
  const std::unique_ptr<std::remove_pointer_t<HMENU>, MenuDeleter> owned_menu(menu);
  AppendMenuW(menu, MF_STRING, open_command, L"Open PaneLive Companion");
  AppendMenuW(menu, MF_STRING, pair_command, L"Pair new client");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  const auto mode = actions_.interface_mode();
  AppendMenuW(menu,
              MF_STRING | (mode == ManagementInterfaceMode::basic ? MF_CHECKED : 0),
              basic_command,
              L"Basic interface");
  AppendMenuW(menu,
              MF_STRING | (mode == ManagementInterfaceMode::advanced ? MF_CHECKED : 0),
              advanced_command,
              L"Advanced interface");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, shutdown_command, L"Shutdown Companion");
  POINT position{};
  GetCursorPos(&position);
  SetForegroundWindow(window_);
  TrackPopupMenu(menu,
                 TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                 position.x,
                 position.y,
                 0,
                 window_,
                 nullptr);
}
} // namespace studio_remote::windows
