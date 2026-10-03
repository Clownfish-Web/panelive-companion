#pragma once

#include "winui_page_helpers.hpp"

namespace studio_remote::windows
{
winrt::Microsoft::UI::Xaml::FrameworkElement make_clients_page(WinUiPageContext context);
void refresh_clients_page(const winrt::Microsoft::UI::Xaml::FrameworkElement& page,
                          WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement make_teleprompter_page(WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement make_teleprompter_sharing(WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement
make_teleprompter_sharing_actions(WinUiPageContext context);
void refresh_teleprompter_page(const winrt::Microsoft::UI::Xaml::FrameworkElement& page,
                               WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement make_adapters_page(WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement make_companion_settings_page(WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement make_status_page(WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement make_diagnostics_page(WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement make_advanced_settings_page(WinUiPageContext context);
winrt::Microsoft::UI::Xaml::FrameworkElement make_about_page(std::string_view build_version);

// Refresh status and immediately applied settings without assigning to draft form controls.
void refresh_adapters_page(const winrt::Microsoft::UI::Xaml::FrameworkElement& page,
                           WinUiPageContext context);
void refresh_companion_settings_page(const winrt::Microsoft::UI::Xaml::FrameworkElement& page,
                                     WinUiPageContext context);
void refresh_advanced_settings_page(const winrt::Microsoft::UI::Xaml::FrameworkElement& page,
                                    WinUiPageContext context);
void refresh_diagnostics_page(const winrt::Microsoft::UI::Xaml::FrameworkElement& page,
                              WinUiPageContext context);
} // namespace studio_remote::windows
