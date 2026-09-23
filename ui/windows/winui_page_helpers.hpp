#pragma once

#include <studio_remote/ui/management_controller.hpp>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace studio_remote::windows
{
struct WinUiPageContext
{
  ManagementUiController& controller;
  std::function<void()> redraw;
  std::function<void(std::wstring, std::wstring, std::function<void()>)> confirm;
  std::function<void()> enlarge_pairing;
};

winrt::Microsoft::UI::Xaml::FrameworkElement load_xaml(std::wstring_view markup);
// Dialog popups do not inherit the management page's requested theme.
winrt::Microsoft::UI::Xaml::Controls::ContentDialog make_management_dialog();
winrt::Microsoft::UI::Xaml::Media::SolidColorBrush management_background_brush();
winrt::Microsoft::UI::Xaml::Media::SolidColorBrush management_navigation_brush();
winrt::Microsoft::UI::Xaml::Media::SolidColorBrush management_separator_brush();
winrt::Microsoft::UI::Xaml::Media::FontFamily management_icon_font();
winrt::Microsoft::UI::Xaml::Controls::Border make_status_badge(std::string_view text,
                                                               std::string_view severity);
winrt::Microsoft::UI::Xaml::Controls::TextBlock
make_table_text(std::string_view text, bool muted = false, bool semibold = false);
std::wstring wide(std::string_view text);
std::string narrow(std::wstring_view text);
std::optional<std::uint16_t> parse_port(const winrt::hstring& text);
std::optional<std::string> choose_directory(winrt::Microsoft::UI::Xaml::XamlRoot xaml_root,
                                            std::string_view initial_directory);
bool save_text_file(winrt::Microsoft::UI::Xaml::XamlRoot xaml_root,
                    std::string_view suggested_name,
                    std::string_view contents);

std::string selected_choice(const winrt::Microsoft::UI::Xaml::Controls::ComboBox& field);
// Preserve selection by identity, including an unavailable selection, across device discovery.
void refresh_choices(const winrt::Microsoft::UI::Xaml::Controls::ComboBox& field,
                     std::vector<std::pair<std::string, std::string>> choices,
                     std::string_view initial_selection = {});

template <typename Control>
Control find_control(const winrt::Microsoft::UI::Xaml::FrameworkElement& root,
                     std::wstring_view name)
{
  return root.FindName(winrt::hstring(name)).as<Control>();
}

template <typename Control>
Control mounted_control(const winrt::Microsoft::UI::Xaml::FrameworkElement& root,
                        std::wstring_view host_name)
{
  return find_control<winrt::Microsoft::UI::Xaml::Controls::Grid>(root, host_name)
      .Children()
      .GetAt(0)
      .as<Control>();
}
} // namespace studio_remote::windows
