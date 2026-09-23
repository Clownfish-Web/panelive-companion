#pragma once

#include "winui_management_pages.hpp"

#include <map>
#include <optional>
#include <string>

namespace studio_remote::windows
{
// Owned by the management window and used only on its UI thread. Editable pages retain their
// controls, including uncommitted NumberBox text and passwords, until the window is destroyed.
class WinUiManagementPagesHost
{
public:
  WinUiManagementPagesHost(winrt::Microsoft::UI::Xaml::Controls::Grid header_host,
                           winrt::Microsoft::UI::Xaml::Controls::Grid body_host,
                           std::string build_version);
  void show(ManagementPage page, WinUiPageContext context);

private:
  struct PageView
  {
    winrt::Microsoft::UI::Xaml::UIElement header{nullptr};
    winrt::Microsoft::UI::Xaml::FrameworkElement body{nullptr};
  };
  PageView create(ManagementPage page, WinUiPageContext context) const;

  winrt::Microsoft::UI::Xaml::Controls::Grid header_host_;
  winrt::Microsoft::UI::Xaml::Controls::Grid body_host_;
  std::string build_version_;
  std::map<ManagementPage, PageView> forms_;
  std::optional<PageView> teleprompter_form_;
  std::optional<teleprompter::SessionAssignment> teleprompter_sharing_selection_;
  std::uint64_t teleprompter_form_generation_ = 0;
};
} // namespace studio_remote::windows
