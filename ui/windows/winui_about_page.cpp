#include "winui_management_pages.hpp"

#include <array>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
struct OpenSourceAttribution
{
  std::wstring_view component;
  std::wstring_view license;
};

constexpr std::array open_source_attributions{
    OpenSourceAttribution{L"CivetWeb 1.16", L"MIT"},
    OpenSourceAttribution{L"FFmpeg 9.0.1", L"LGPL-2.1-or-later"},
    OpenSourceAttribution{L"libdatachannel 0.24.5 and libjuice 1.7.2", L"MPL-2.0"},
    OpenSourceAttribution{L"libomt 1.0.0.16", L"MIT"},
    OpenSourceAttribution{L"libqrencode 4.1.1", L"LGPL-2.1-or-later"},
    OpenSourceAttribution{L"libsrt 1.5.6", L"MPL-2.0"},
    OpenSourceAttribution{L"libsrtp 2.8.0", L"BSD-3-Clause"},
    OpenSourceAttribution{L"libvpx 1.16.0", L"BSD-3-Clause"},
    OpenSourceAttribution{L"nlohmann/json 3.12.0", L"MIT"},
    OpenSourceAttribution{L"OpenSSL 3.6.3", L"Apache-2.0"},
    OpenSourceAttribution{L"Opus 1.5.2", L"BSD-3-Clause"},
    OpenSourceAttribution{L"plog 1.1.11", L"MIT"},
    OpenSourceAttribution{L"SQLite 3.53.4", L"Public Domain"},
    OpenSourceAttribution{L"usrsctp 0.9.5.0", L"BSD-3-Clause"},
    OpenSourceAttribution{L"zlib 1.3.2", L"Zlib"}};

constexpr wchar_t about_markup[] = LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RowSpacing="18">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel Spacing="4"><TextBlock Text="About" FontSize="28" FontWeight="SemiBold"/><TextBlock Text="Build information, licensing, and acknowledgements." Opacity="0.68"/></StackPanel>
  <Border Grid.Row="1" Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
    <StackPanel Spacing="8">
      <StackPanel Orientation="Horizontal" Spacing="10">
        <TextBlock Text="PaneLive Companion" FontSize="22" FontWeight="SemiBold" VerticalAlignment="Center"/>
        <Border Background="#493C16" CornerRadius="10" Padding="8,3"><TextBlock Text="Beta" Foreground="#FFE082" FontWeight="SemiBold"/></Border>
      </StackPanel>
      <TextBlock x:Name="Version" FontSize="16"/>
      <TextBlock Text="Native sidecar gateway for secure remote production monitoring and control." TextWrapping="Wrap" Opacity="0.78"/>
      <TextBlock Text="Licensed under the Apache License 2.0." Margin="0,8,0,0"/>
      <TextBlock Text="Copyright 2026 Clownfish Web Artur Cichosz and contributors." Opacity="0.68"/>
    </StackPanel>
  </Border>
  <StackPanel Grid.Row="2" Spacing="10">
    <TextBlock Text="Open-source acknowledgements" FontSize="18" FontWeight="SemiBold"/>
    <TextBlock Text="PaneLive Companion is built with the following open-source software." TextWrapping="Wrap" Opacity="0.78"/>
    <Border Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="16">
      <StackPanel x:Name="Attributions" Spacing="8"/>
    </Border>
    <TextBlock Text="Complete notices and license texts are provided in THIRD_PARTY_NOTICES.md and the installed share/panelive-companion/licenses directory." TextWrapping="Wrap" Opacity="0.68"/>
    <TextBlock Text="Windows App SDK and WinUI are used under the Microsoft Software License Terms." TextWrapping="Wrap" Opacity="0.68"/>
  </StackPanel>
</Grid>)xaml";
} // namespace

xaml::FrameworkElement make_about_page(std::string_view build_version)
{
  const auto page = load_xaml(about_markup);
  find_control<controls::TextBlock>(page, L"Version").Text(L"Version " + wide(build_version));

  const auto attributions = find_control<controls::StackPanel>(page, L"Attributions");
  for (const auto& attribution : open_source_attributions)
  {
    const auto row = controls::Grid();
    row.ColumnSpacing(16);
    controls::ColumnDefinition component_column;
    component_column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
    controls::ColumnDefinition license_column;
    license_column.Width(xaml::GridLengthHelper::FromPixels(180));
    row.ColumnDefinitions().Append(component_column);
    row.ColumnDefinitions().Append(license_column);

    const auto component = controls::TextBlock();
    component.Text(winrt::hstring(attribution.component));
    component.TextWrapping(xaml::TextWrapping::Wrap);
    row.Children().Append(component);

    const auto license = controls::TextBlock();
    license.Text(winrt::hstring(attribution.license));
    license.Opacity(0.68);
    controls::Grid::SetColumn(license, 1);
    row.Children().Append(license);
    attributions.Children().Append(row);
  }
  return page;
}
} // namespace studio_remote::windows
