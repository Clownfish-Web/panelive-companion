#include "winui_pairing_preview.hpp"

#include "winui_management_window.hpp"
#include "winui_page_helpers.hpp"

#include <robuffer.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <cstring>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
std::uint64_t unix_seconds()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

xaml::FrameworkElement make_preview_panel(const PairingQrImage& image,
                                          const std::function<void()>& close)
{
  const auto panel = load_xaml(LR"xaml(
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Background="#1B212A" CornerRadius="8" Padding="6"
        HorizontalAlignment="Center" VerticalAlignment="Center">
  <StackPanel Spacing="4">
    <Grid ColumnSpacing="12">
      <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
      <TextBlock Text="Pair a new client" FontWeight="SemiBold" VerticalAlignment="Center"/>
      <Button x:Name="ClosePreview" Grid.Column="1" Width="36" Height="36" Padding="0"
              HorizontalAlignment="Right" Foreground="White" Background="#343E4C"
              ToolTipService.ToolTip="Dismiss enlarged QR (Esc)"
              AutomationProperties.Name="Close enlarged pairing QR"/>
    </Grid>
    <Viewbox x:Name="PreviewImage"/>
    <TextBlock x:Name="Countdown" HorizontalAlignment="Center"/>
  </StackPanel>
</Border>)xaml");
  panel.Width(PairingPreview::enlarged_size + 12);
  const auto image_host = find_control<controls::Viewbox>(panel, L"PreviewImage");
  image_host.Width(PairingPreview::enlarged_size);
  image_host.Height(PairingPreview::enlarged_size);
  image_host.Child(make_pairing_image(image, PairingPreview::enlarged_size));
  const auto close_button = find_control<controls::Button>(panel, L"ClosePreview");
  const auto icon = controls::FontIcon();
  icon.FontFamily(management_icon_font());
  icon.FontSize(16);
  icon.Glyph(L"\xE711");
  close_button.Content(icon);
  close_button.Click([close](const auto&, const auto&) { close(); });
  const auto escape = xaml::Input::KeyboardAccelerator();
  escape.Key(winrt::Windows::System::VirtualKey::Escape);
  escape.Invoked(
      [close](const auto&, const auto& event)
      {
        event.Handled(true);
        close();
      });
  close_button.KeyboardAccelerators().Append(escape);
  return panel;
}
} // namespace

controls::Image make_pairing_image(const PairingQrImage& qr, int size)
{
  const auto scaled = resample_pairing_qr_image(qr, size);
  const auto bitmap = xaml::Media::Imaging::WriteableBitmap(size, size);
  std::uint8_t* destination = nullptr;
  winrt::check_hresult(
      bitmap.PixelBuffer().as<::Windows::Storage::Streams::IBufferByteAccess>()->Buffer(
          &destination));
  std::memcpy(destination, scaled.pixels.data(), scaled.pixels.size());
  bitmap.Invalidate();
  const auto image = controls::Image();
  image.Width(size);
  image.Height(size);
  image.Source(bitmap);
  image.Stretch(xaml::Media::Stretch::Uniform);
  return image;
}

void WinUiManagementWindow::open_pairing_preview()
{
  close_pairing_preview();
  if (current_page_ != ManagementPage::clients || !controller_.state().pairing)
    return;
  try
  {
    pairing_preview_source_ = controller_.state().pairing;
    pairing_preview_image_ = make_pairing_qr_image(pairing_preview_source_->payload);
    pairing_preview_.open(pairing_preview_image_.pixels,
                          pairing_preview_source_->expires_unix_seconds,
                          std::chrono::steady_clock::now(),
                          unix_seconds());
    const auto panel =
        make_preview_panel(pairing_preview_image_, [this] { close_pairing_preview(); });
    pairing_preview_countdown_ = find_control<controls::TextBlock>(panel, L"Countdown");
    pairing_preview_host_.Background(
        xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{166, 0, 0, 0}));
    pairing_preview_host_.Children().Append(panel);
    pairing_preview_host_.Visibility(xaml::Visibility::Visible);
    content_control_.IsEnabled(false);
    update_pairing_preview();
    if (!pairing_preview_source_)
      return;
    pairing_preview_timer_ = xaml::DispatcherTimer();
    pairing_preview_timer_.Interval(std::chrono::milliseconds(100));
    pairing_preview_timer_.Tick(
        [this](const auto&, const auto&)
        {
          try
          {
            update_pairing_preview();
          }
          catch (...)
          {
            OutputDebugStringW(L"Could not update enlarged pairing QR.\n");
            close_pairing_preview();
          }
        });
    pairing_preview_timer_.Start();
    find_control<controls::Button>(panel, L"ClosePreview").Focus(xaml::FocusState::Programmatic);
  }
  catch (...)
  {
    close_pairing_preview();
    controller_.report_error("Could not display enlarged pairing QR");
    OutputDebugStringW(L"Could not display enlarged pairing QR.\n");
    redraw_page();
  }
}

void WinUiManagementWindow::update_pairing_preview()
{
  if (!pairing_preview_source_)
    return;
  const auto& current = controller_.state().pairing;
  if (current_page_ != ManagementPage::clients || !current ||
      current->payload != pairing_preview_source_->payload ||
      current->expires_unix_seconds != pairing_preview_source_->expires_unix_seconds)
  {
    close_pairing_preview();
    return;
  }
  const auto remaining = pairing_preview_.seconds_remaining(pairing_preview_image_.pixels,
                                                            current->expires_unix_seconds,
                                                            std::chrono::steady_clock::now(),
                                                            unix_seconds());
  if (remaining == 0)
  {
    close_pairing_preview();
    return;
  }
  const auto text = L"Closing in " + std::to_wstring(remaining) + L" seconds";
  if (pairing_preview_countdown_.Text() != text)
    pairing_preview_countdown_.Text(text);
}

void WinUiManagementWindow::close_pairing_preview()
{
  if (pairing_preview_timer_)
    pairing_preview_timer_.Stop();
  pairing_preview_timer_ = nullptr;
  pairing_preview_.close();
  pairing_preview_source_.reset();
  pairing_preview_image_ = {};
  pairing_preview_host_.Visibility(xaml::Visibility::Collapsed);
  pairing_preview_host_.Children().Clear();
  pairing_preview_countdown_ = nullptr;
  content_control_.IsEnabled(true);
}
} // namespace studio_remote::windows
