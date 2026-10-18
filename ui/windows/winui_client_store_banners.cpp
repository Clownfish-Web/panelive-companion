#include "winui_client_store_banners.hpp"

#include "winui_page_helpers.hpp"
#include "../../platform/windows/resources/windows_resource_ids.h"
#include "client_stores.hpp"

#include <robuffer.h>
#include <wincodec.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
xaml::Media::Imaging::WriteableBitmap client_icon()
{
  constexpr int size = 64;
  const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
      MAKEINTRESOURCEW(IDI_PANELIVE_COMPANION), IMAGE_ICON, size, size,
      LR_DEFAULTCOLOR | LR_SHARED));
  winrt::check_bool(icon != nullptr);
  const auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
  winrt::com_ptr<IWICBitmap> source;
  winrt::check_hresult(factory->CreateBitmapFromHICON(icon, source.put()));
  winrt::com_ptr<IWICFormatConverter> converter;
  winrt::check_hresult(factory->CreateFormatConverter(converter.put()));
  winrt::check_hresult(converter->Initialize(source.get(), GUID_WICPixelFormat32bppPBGRA,
      WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
  const auto bitmap = xaml::Media::Imaging::WriteableBitmap(size, size);
  std::uint8_t* pixels = nullptr;
  winrt::check_hresult(bitmap.PixelBuffer().as<::Windows::Storage::Streams::IBufferByteAccess>()
      ->Buffer(&pixels));
  winrt::check_hresult(converter->CopyPixels(nullptr, size * 4, size * size * 4, pixels));
  bitmap.Invalidate();
  return bitmap;
}

controls::HyperlinkButton promo_banner(std::wstring_view title, const char* url,
                                      const xaml::Media::ImageSource& icon,
                                      std::wstring_view caption = L"View on")
{
  const auto banner = load_xaml(LR"xaml(
<HyperlinkButton xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Padding="10"
    HorizontalAlignment="Stretch" HorizontalContentAlignment="Stretch"
    Background="#242C38" BorderBrush="#445166" BorderThickness="1" CornerRadius="8">
  <Grid ColumnSpacing="10">
    <Grid.ColumnDefinitions><ColumnDefinition Width="32"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
    <Image x:Name="AppIcon" Width="32" Height="32" Stretch="Uniform"/>
    <StackPanel Grid.Column="1" Spacing="2">
      <TextBlock x:Name="Action" FontSize="10" Foreground="#B8C4D6"/>
      <TextBlock x:Name="Store" FontSize="15" FontWeight="SemiBold" Foreground="White"/>
    </StackPanel>
  </Grid>
</HyperlinkButton>)xaml").as<controls::HyperlinkButton>();
  find_control<controls::Image>(banner, L"AppIcon").Source(icon);
  find_control<controls::TextBlock>(banner, L"Store").Text(title);
  find_control<controls::TextBlock>(banner, L"Action").Text(caption);
  const auto label = std::wstring(caption) + L" " + std::wstring(title);
  xaml::Automation::AutomationProperties::SetName(banner, label);
  controls::ToolTipService::SetToolTip(banner, winrt::box_value(label));
  if (*url)
    banner.NavigateUri(winrt::Windows::Foundation::Uri(wide(url)));
  else
  {
    banner.IsEnabled(false);
    find_control<controls::TextBlock>(banner, L"Action").Text(L"Coming soon on");
  }
  return banner;
}
} // namespace

controls::StackPanel make_client_store_banners()
{
  const auto panel = controls::StackPanel();
  panel.Spacing(8);
  panel.Margin(xaml::Thickness{0, 18, 0, 12});
  const auto icon = client_icon();
  panel.Children().Append(promo_banner(L"PaneLive Stack", "https://panelive.app", icon,
                                      L"Learn more about"));
  const auto heading = controls::TextBlock();
  heading.Text(L"Get PaneLive Client");
  heading.FontSize(12);
  heading.Margin(xaml::Thickness{2, 10, 0, 2});
  panel.Children().Append(heading);
  panel.Children().Append(promo_banner(L"App Store", client_stores::app_store_url, icon));
  panel.Children().Append(promo_banner(L"Google Play", client_stores::play_store_url, icon));
  return panel;
}
} // namespace studio_remote::windows
