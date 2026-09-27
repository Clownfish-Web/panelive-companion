#include "winui_management_window.hpp"

#include "winui_management_pages.hpp"
#include <studio_remote/ui/management_approval_dispatcher.hpp>

#include "../../platform/windows/resources/windows_resource_ids.h"

#include <dwmapi.h>
#include <microsoft.ui.xaml.window.h>

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
constexpr DWORD minimum_mica_build = 22'000;
constexpr DWORD immersive_dark_mode_attribute = 20;
constexpr DWORD legacy_immersive_dark_mode_attribute = 19;

struct WinUiApplication : xaml::ApplicationT<WinUiApplication, xaml::Markup::IXamlMetadataProvider>
{
  explicit WinUiApplication(std::function<void()> launched) : launched_(std::move(launched))
  {
  }

  void OnLaunched(const xaml::LaunchActivatedEventArgs&)
  {
    Resources().MergedDictionaries().Append(controls::XamlControlsResources());
    launched_();
  }

  auto GetXamlType(const winrt::Windows::UI::Xaml::Interop::TypeName& type) const
  {
    return metadata_provider_.GetXamlType(type);
  }

  auto GetXamlType(const winrt::hstring& full_name) const
  {
    return metadata_provider_.GetXamlType(full_name);
  }

  auto GetXmlnsDefinitions() const
  {
    return metadata_provider_.GetXmlnsDefinitions();
  }

private:
  std::function<void()> launched_;
  xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider metadata_provider_;
};

bool supports_mica_backdrop()
{
  using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto rtl_get_version =
      ntdll ? reinterpret_cast<RtlGetVersion>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  if (!rtl_get_version)
    return false;

  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  if (rtl_get_version(&version) != 0)
    return false;
  return version.dwMajorVersion > 10 ||
         (version.dwMajorVersion == 10 && version.dwBuildNumber >= minimum_mica_build);
}

HICON companion_icon()
{
  return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                       MAKEINTRESOURCEW(IDI_PANELIVE_COMPANION),
                                       IMAGE_ICON,
                                       32,
                                       32,
                                       LR_DEFAULTCOLOR | LR_SHARED));
}

void enable_dark_title_bar(HWND window)
{
  const BOOL enabled = TRUE;
  auto result =
      DwmSetWindowAttribute(window, immersive_dark_mode_attribute, &enabled, sizeof(enabled));
  if (FAILED(result))
  {
    result = DwmSetWindowAttribute(
        window, legacy_immersive_dark_mode_attribute, &enabled, sizeof(enabled));
  }
  if (FAILED(result))
    OutputDebugStringW(L"PaneLive Companion could not enable the dark native title bar.\n");
}

} // namespace

void run_winui_application(std::function<void()> launched)
{
  xaml::Application::Start([launched = std::move(launched)](const auto&) mutable
                           { winrt::make<WinUiApplication>(std::move(launched)); });
}

WinUiManagementWindow::WinUiManagementWindow(std::string gateway_name,
                                             std::string build_version,
                                             ManagementUiController& controller)
    : gateway_name_(std::move(gateway_name)), build_version_(std::move(build_version)),
      controller_(controller)
{
  window_ = xaml::Window();
  window_.Title(L"PaneLive Companion");
  if (supports_mica_backdrop())
    window_.SystemBackdrop(xaml::Media::MicaBackdrop());

  shell_ = controls::Grid();
  shell_.RequestedTheme(xaml::ElementTheme::Dark);
  shell_.Background(management_background_brush());
  controls::ColumnDefinition navigation_column;
  navigation_column.Width(xaml::GridLengthHelper::FromPixels(232));
  controls::ColumnDefinition content_column;
  content_column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
  shell_.ColumnDefinitions().Append(navigation_column);
  shell_.ColumnDefinitions().Append(content_column);

  const auto navigation_background = controls::Border();
  navigation_background.Background(management_navigation_brush());
  navigation_background.BorderBrush(management_separator_brush());
  navigation_background.BorderThickness(xaml::Thickness{0, 0, 1, 0});
  const auto navigation_layout = controls::Grid();
  navigation_layout.Padding(xaml::Thickness{12, 20, 12, 12});
  controls::RowDefinition navigation_title_row;
  navigation_title_row.Height(xaml::GridLengthHelper::Auto());
  controls::RowDefinition primary_navigation_row;
  primary_navigation_row.Height(
      xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
  controls::RowDefinition footer_navigation_row;
  footer_navigation_row.Height(xaml::GridLengthHelper::Auto());
  navigation_layout.RowDefinitions().Append(navigation_title_row);
  navigation_layout.RowDefinitions().Append(primary_navigation_row);
  navigation_layout.RowDefinitions().Append(footer_navigation_row);
  const auto navigation_title = controls::TextBlock();
  navigation_title.Text(L"PaneLive");
  navigation_title.FontSize(20);
  navigation_title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
  navigation_title.Margin(xaml::Thickness{12, 0, 0, 18});
  navigation_layout.Children().Append(navigation_title);
  primary_navigation_ = controls::StackPanel();
  primary_navigation_.Spacing(2);
  controls::Grid::SetRow(primary_navigation_, 1);
  navigation_layout.Children().Append(primary_navigation_);
  footer_navigation_ = controls::StackPanel();
  controls::Grid::SetRow(footer_navigation_, 2);
  navigation_layout.Children().Append(footer_navigation_);
  navigation_background.Child(navigation_layout);
  shell_.Children().Append(navigation_background);

  // A Control parent disables both pointer and keyboard interaction during the QR preview.
  content_control_ = controls::ContentControl();
  content_control_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  content_control_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
  content_control_.IsTabStop(false);
  controls::Grid::SetColumn(content_control_, 1);
  content_root_ = controls::Grid();
  content_root_.RequestedTheme(xaml::ElementTheme::Dark);
  content_root_.Background(management_background_brush());
  content_root_.Padding(xaml::Thickness{28, 0, 28, 28});
  content_root_.RowSpacing(10);
  controls::RowDefinition notices;
  notices.Height(xaml::GridLengthHelper::Auto());
  controls::RowDefinition page_header;
  page_header.Height(xaml::GridLengthHelper::Auto());
  controls::RowDefinition page;
  page.Height(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
  content_root_.RowDefinitions().Append(notices);
  content_root_.RowDefinitions().Append(page_header);
  content_root_.RowDefinitions().Append(page);

  const auto notice_stack = controls::StackPanel();
  notice_stack.Spacing(8);
  error_bar_ = controls::InfoBar();
  error_bar_.Severity(controls::InfoBarSeverity::Error);
  error_bar_.IsClosable(false);
  error_bar_.Title(L"Management operation failed");
  recovery_bar_ = controls::InfoBar();
  recovery_bar_.Severity(controls::InfoBarSeverity::Warning);
  recovery_bar_.IsClosable(false);
  recovery_bar_.Title(L"Companion listener needs attention");
  const auto recovery_action = controls::Button();
  recovery_action.Content(winrt::box_value(L"Open Companion Settings"));
  recovery_action.Click(
      [this](const auto&, const auto&)
      {
        controller_.set_interface_mode(ManagementInterfaceMode::advanced);
        select_page(ManagementPage::companion_settings);
      });
  recovery_bar_.ActionButton(recovery_action);
  notice_stack.Children().Append(error_bar_);
  notice_stack.Children().Append(recovery_bar_);
  content_root_.Children().Append(notice_stack);
  page_header_host_ = controls::Grid();
  controls::Grid::SetRow(page_header_host_, 1);
  content_root_.Children().Append(page_header_host_);
  page_scroll_ = controls::ScrollViewer();
  page_scroll_.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Auto);
  page_scroll_.HorizontalScrollBarVisibility(controls::ScrollBarVisibility::Disabled);
  page_scroll_.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
  page_scroll_.VerticalContentAlignment(xaml::VerticalAlignment::Stretch);
  controls::Grid::SetRow(page_scroll_, 2);
  page_host_ = controls::Grid();
  page_scroll_.Content(page_host_);
  pages_ =
      std::make_unique<WinUiManagementPagesHost>(page_header_host_, page_host_, build_version_);
  content_root_.Children().Append(page_scroll_);
  content_control_.Content(content_root_);
  shell_.Children().Append(content_control_);
  pairing_preview_host_ = controls::Grid();
  controls::Grid::SetColumn(pairing_preview_host_, 1);
  pairing_preview_host_.Visibility(xaml::Visibility::Collapsed);
  shell_.Children().Append(pairing_preview_host_);
  window_.Content(shell_);

  refresh_timer_ = xaml::DispatcherTimer();
  refresh_timer_.Interval(std::chrono::seconds(5));
  refresh_timer_.Tick(
      [this](const auto&, const auto&)
      {
        controller_.refresh();
        const auto hwnd = window_handle();
        if (IsWindowVisible(hwnd) && !IsIconic(hwnd))
          redraw_page();
      });

  const auto app_window = window_.AppWindow();
  const auto title_bar = app_window.TitleBar();
  title_bar.BackgroundColor(winrt::Windows::UI::Color{255, 24, 27, 32});
  title_bar.ForegroundColor(winrt::Windows::UI::Color{255, 255, 255, 255});
  title_bar.ButtonBackgroundColor(winrt::Windows::UI::Color{255, 24, 27, 32});
  title_bar.ButtonForegroundColor(winrt::Windows::UI::Color{255, 255, 255, 255});
  title_bar.ButtonHoverBackgroundColor(winrt::Windows::UI::Color{255, 52, 58, 68});
  title_bar.ButtonPressedBackgroundColor(winrt::Windows::UI::Color{255, 70, 77, 89});
  title_bar.ButtonInactiveBackgroundColor(winrt::Windows::UI::Color{255, 24, 27, 32});
  app_window.Resize({1120, 760});
  app_window.Closing(
      [this](const auto&, const winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs& event)
      {
        if (shutting_down_)
          return;
        event.Cancel(true);
        hide();
      });
  if (const auto hwnd = window_handle())
  {
    enable_dark_title_bar(hwnd);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(companion_icon()));
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(companion_icon()));
    const auto screen_width = GetSystemMetrics(SM_CXSCREEN);
    const auto screen_height = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd,
                 nullptr,
                 std::max(0, (screen_width - 1120) / 2),
                 std::max(0, (screen_height - 760) / 2),
                 1120,
                 760,
                 SWP_NOACTIVATE | SWP_NOZORDER);
  }
  current_page_ = controller_.state().initial_page;
  rebuild_navigation();
  redraw_page();
  refresh_timer_.Start();
}

void WinUiManagementWindow::activate()
{
  controller_.refresh();
  rebuild_navigation();
  redraw_page();
  window_.Activate();
  if (const auto hwnd = window_handle())
  {
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
  }
  refresh_timer_.Start();
}

void WinUiManagementWindow::hide()
{
  cancel_client_identification();
  close_pairing_preview();
  if (const auto hwnd = window_handle())
    ShowWindow(hwnd, SW_HIDE);
}

void WinUiManagementWindow::close()
{
  cancel_client_identification();
  for (auto* dialogs : {role_dialogs_.get(), global_dialogs_.get()})
    while (!dialogs->empty())
    {
      const auto key = dialogs->begin()->first;
      cancel_approval(*dialogs, key.first, key.second);
    }
  close_pairing_preview();
  shutting_down_ = true;
  refresh_timer_.Stop();
  window_.Close();
  xaml::Application::Current().Exit();
}

void WinUiManagementWindow::refresh()
{
  controller_.refresh();
  redraw_page();
}

void WinUiManagementWindow::report_approval_error(std::string_view message)
{
  report_management_approval_error(controller_, message);
  redraw_page();
}

void WinUiManagementWindow::show_pairing(ManagementPairing pairing)
{
  close_pairing_preview();
  controller_.show_pairing(std::move(pairing));
  select_page(ManagementPage::clients);
  activate();
}

void WinUiManagementWindow::clear_pairing()
{
  close_pairing_preview();
  controller_.clear_pairing();
  if (current_page_ == ManagementPage::clients)
    redraw_page();
}

void WinUiManagementWindow::show_confirmation(std::wstring title,
                                              std::wstring message,
                                              std::function<void()> confirmed)
{
  const auto dialog = make_management_dialog();
  dialog.XamlRoot(shell_.XamlRoot());
  dialog.Title(winrt::box_value(winrt::hstring(title)));
  dialog.Content(winrt::box_value(winrt::hstring(message)));
  dialog.PrimaryButtonText(L"Continue");
  dialog.CloseButtonText(L"Cancel");
  dialog.DefaultButton(controls::ContentDialogButton::Close);
  const auto operation = dialog.ShowAsync();
  operation.Completed(
      [confirmed = std::move(confirmed)](const auto& result,
                                         winrt::Windows::Foundation::AsyncStatus status)
      {
        if (status == winrt::Windows::Foundation::AsyncStatus::Completed &&
            result.GetResults() == controls::ContentDialogResult::Primary)
          confirmed();
      });
}

HWND WinUiManagementWindow::window_handle() const
{
  HWND window = nullptr;
  winrt::check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(&window));
  return window;
}

void WinUiManagementWindow::rebuild_navigation()
{
  primary_navigation_.Children().Clear();
  footer_navigation_.Children().Clear();
  const bool advanced = controller_.state().interface_mode == ManagementInterfaceMode::advanced;
  primary_navigation_.Children().Append(
      make_navigation_button(ManagementPage::clients, L"Clients", L'\xE70A', L'\xE8EA'));
  primary_navigation_.Children().Append(
      make_navigation_button(ManagementPage::adapters, L"Adapters", L'\xE968'));
  primary_navigation_.Children().Append(
      make_navigation_button(ManagementPage::teleprompter, L"Teleprompter", L'\xE8A5'));
  if (advanced)
    primary_navigation_.Children().Append(make_navigation_button(
        ManagementPage::companion_settings, L"Companion Settings", L'\xE774'));
  primary_navigation_.Children().Append(
      make_navigation_button(ManagementPage::status, L"Status", L'\xE9D9'));
  if (advanced)
    primary_navigation_.Children().Append(
        make_navigation_button(ManagementPage::diagnostics, L"Diagnostics", L'\xE90F'));
  current_page_ = visible_management_page(controller_.state().interface_mode, current_page_);
  footer_navigation_.Children().Append(
      make_navigation_button(ManagementPage::advanced_settings, L"Advanced Settings", L'\xE713'));
  footer_navigation_.Children().Append(
      make_navigation_button(ManagementPage::about, L"About", L'\xE946'));
}

void WinUiManagementWindow::select_page(ManagementPage page)
{
  if (page != current_page_ && controller_.teleprompter().has_unsaved_changes())
  {
    show_confirmation(L"Discard script changes",
                      L"Discard the unsaved teleprompter script changes?",
                      [this, page]
                      {
                        controller_.teleprompter().close();
                        select_page(page);
                      });
    return;
  }
  if (page != current_page_)
  {
    controller_.teleprompter().close();
    if (page == ManagementPage::teleprompter)
      controller_.teleprompter().open_library();
  }
  close_pairing_preview();
  current_page_ = visible_management_page(controller_.state().interface_mode, page);
  rebuild_navigation();
  redraw_page();
}

void WinUiManagementWindow::redraw_page()
{
  update_pairing_preview();
  const auto& state = controller_.state();
  error_bar_.IsOpen(!state.last_error.empty());
  error_bar_.Message(wide(state.last_error));
  recovery_bar_.IsOpen(!state.network_settings.listening &&
                       state.interface_mode == ManagementInterfaceMode::basic);
  recovery_bar_.Message(wide(state.network_settings.warning.empty()
                                 ? "The Companion listener needs attention."
                                 : state.network_settings.warning));
  const WinUiPageContext context{
      controller_,
      [this]
      {
        rebuild_navigation();
        redraw_page();
      },
      [this](std::wstring title, std::wstring message, std::function<void()> confirmed)
      { show_confirmation(std::move(title), std::move(message), std::move(confirmed)); },
      [this] { open_pairing_preview(); }};
  pages_->show(current_page_, context);
  show_client_identification();
}

controls::Button WinUiManagementWindow::make_navigation_button(ManagementPage page,
                                                               std::wstring_view label,
                                                               wchar_t glyph,
                                                               wchar_t secondary_glyph)
{
  const auto button = controls::Button();
  button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  button.HorizontalContentAlignment(xaml::HorizontalAlignment::Left);
  button.Height(42);
  button.Padding(xaml::Thickness{12, 0, 12, 0});
  button.CornerRadius(xaml::CornerRadius{4});
  button.Background(xaml::Media::SolidColorBrush(current_page_ == page
                                                     ? winrt::Windows::UI::Color{30, 255, 255, 255}
                                                     : winrt::Windows::UI::Color{0, 0, 0, 0}));
  const auto content = controls::StackPanel();
  content.Orientation(controls::Orientation::Horizontal);
  content.Spacing(12);
  const auto icon_host = controls::Grid();
  icon_host.Width(20);
  icon_host.Height(20);
  const auto primary_icon = controls::FontIcon();
  primary_icon.FontFamily(management_icon_font());
  const wchar_t glyph_text[]{glyph, L'\0'};
  primary_icon.Glyph(glyph_text);
  primary_icon.FontSize(secondary_glyph == L'\0' ? 16 : 14);
  icon_host.Children().Append(primary_icon);
  if (secondary_glyph != L'\0')
  {
    primary_icon.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    const auto secondary_icon = controls::FontIcon();
    secondary_icon.FontFamily(management_icon_font());
    const wchar_t secondary_glyph_text[]{secondary_glyph, L'\0'};
    secondary_icon.Glyph(secondary_glyph_text);
    secondary_icon.FontSize(11);
    secondary_icon.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    secondary_icon.VerticalAlignment(xaml::VerticalAlignment::Bottom);
    icon_host.Children().Append(secondary_icon);
  }
  const auto text = controls::TextBlock();
  text.Text(winrt::hstring(label));
  text.VerticalAlignment(xaml::VerticalAlignment::Center);
  content.Children().Append(icon_host);
  content.Children().Append(text);
  button.Content(content);
  button.Click([this, page](const auto&, const auto&) { select_page(page); });
  return button;
}
} // namespace studio_remote::windows
