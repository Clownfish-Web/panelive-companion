#include "winui_management_pages.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
constexpr wchar_t adapters_markup[] = LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RowSpacing="18">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel Spacing="4">
    <TextBlock Text="Adapters" FontSize="28" FontWeight="SemiBold"/>
    <TextBlock Text="Configure adapter connections." Opacity="0.68"/>
  </StackPanel>
  <StackPanel Grid.Row="1" Spacing="18">
      <Border x:Name="TeleprompterCard" Background="#1B212A"
              BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="14">
          <TextBlock Text="PaneLive Teleprompter" FontSize="18" FontWeight="SemiBold"/>
          <Border Height="1" Background="#343E4C"/>
          <TextBlock Text="This built-in adapter starts automatically. Pair devices to share scripts and playback. Use Status to start, stop or restart it. No production software or connection settings are needed."
                     TextWrapping="Wrap" Opacity="0.68"/>
        </StackPanel>
      </Border>
      <Border x:Name="ObsCard" Visibility="Collapsed" Background="#1B212A"
              BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="14">
          <Grid><TextBlock Text="OBS Studio" FontSize="18" FontWeight="SemiBold"/><StackPanel x:Name="ObsStatusHost" HorizontalAlignment="Right"/></Grid>
          <Border Height="1" Background="#343E4C"/>
          <Grid ColumnSpacing="12" RowSpacing="10">
            <Grid.ColumnDefinitions>
              <ColumnDefinition Width="150"/>
              <ColumnDefinition Width="*"/>
            </Grid.ColumnDefinitions>
            <Grid.RowDefinitions>
              <RowDefinition Height="Auto"/>
              <RowDefinition Height="Auto"/>
              <RowDefinition Height="Auto"/>
            </Grid.RowDefinitions>
            <TextBlock Text="Host" Opacity="0.68" VerticalAlignment="Center"/>
            <Grid x:Name="ObsHostField" Grid.Column="1"/>
            <TextBlock Grid.Row="1" Text="WebSocket port" Opacity="0.68" VerticalAlignment="Center"/>
            <Grid x:Name="ObsPortField" Grid.Row="1" Grid.Column="1"/>
            <TextBlock Grid.Row="2" Text="Password" Opacity="0.68" VerticalAlignment="Center"/>
            <Grid x:Name="ObsPasswordField" Grid.Row="2" Grid.Column="1"/>
          </Grid>
          <Button x:Name="ApplyObs" Content="Apply OBS settings" Style="{StaticResource AccentButtonStyle}" HorizontalAlignment="Right"/>
        </StackPanel>
      </Border>
      <Border x:Name="VmixCard" Visibility="Collapsed" Background="#1B212A"
              BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="14">
          <Grid><TextBlock Text="vMix" FontSize="18" FontWeight="SemiBold"/><StackPanel x:Name="VmixStatusHost" HorizontalAlignment="Right"/></Grid>
          <Border Height="1" Background="#343E4C"/>
          <Grid ColumnSpacing="12" RowSpacing="10">
            <Grid.ColumnDefinitions><ColumnDefinition Width="150"/><ColumnDefinition Width="*"/><ColumnDefinition Width="150"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
            <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
            <TextBlock Text="Host" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="VmixHostField" Grid.Column="1"/>
            <TextBlock Grid.Column="2" Text="HTTP port" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="VmixHttpPortField" Grid.Column="3"/>
            <TextBlock Grid.Row="1" Text="Username" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="VmixUsernameField" Grid.Row="1" Grid.Column="1"/>
            <TextBlock Grid.Row="1" Grid.Column="2" Text="Password" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="VmixPasswordField" Grid.Row="1" Grid.Column="3"/>
            <TextBlock Grid.Row="2" Text="Program output" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="ProgramOutputField" Grid.Row="2" Grid.Column="1"/>
            <TextBlock Grid.Row="2" Grid.Column="2" Text="Program SRT port" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="ProgramPortField" Grid.Row="2" Grid.Column="3"/>
            <TextBlock Grid.Row="3" Text="Preview output" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="PreviewOutputField" Grid.Row="3" Grid.Column="1"/>
            <TextBlock Grid.Row="3" Grid.Column="2" Text="Preview SRT port" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="PreviewPortField" Grid.Row="3" Grid.Column="3"/>
            <TextBlock Grid.Row="4" Text="SRT latency" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="LatencyField" Grid.Row="4" Grid.Column="1"/>
            <TextBlock Grid.Row="4" Grid.Column="2" Text="milliseconds" Opacity="0.68" VerticalAlignment="Center"/>
          </Grid>
          <Button x:Name="ApplyVmix" Content="Apply vMix settings" Style="{StaticResource AccentButtonStyle}" HorizontalAlignment="Right"/>
        </StackPanel>
      </Border>
      <Border x:Name="StreamlabsCard" Visibility="Collapsed" Background="#1B212A"
              BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="14">
          <TextBlock Text="Streamlabs Desktop" FontSize="18" FontWeight="SemiBold"/>
          <Border Height="1" Background="#343E4C"/>
          <TextBlock Text="Uses a named pipe. No connection or authentication parameters are needed."
                     TextWrapping="Wrap" Opacity="0.68"/>
        </StackPanel>
      </Border>
  </StackPanel>
</Grid>)xaml";

constexpr wchar_t companion_markup[] = LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RowSpacing="18">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel Spacing="4">
    <TextBlock Text="Companion Settings" FontSize="28" FontWeight="SemiBold"/>
    <TextBlock Text="Configure the HTTPS/WSS listener and pairing address." Opacity="0.68"/>
  </StackPanel>
  <StackPanel Grid.Row="1" Spacing="18">
      <Border Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="14">
          <Grid><TextBlock Text="Companion network" FontSize="18" FontWeight="SemiBold"/><StackPanel x:Name="NetworkStatusHost" HorizontalAlignment="Right"/></Grid>
          <Border Height="1" Background="#343E4C"/>
          <Grid x:Name="NetworkWarningHost"/>
          <Grid ColumnSpacing="12" RowSpacing="10">
            <Grid.ColumnDefinitions><ColumnDefinition Width="170"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
            <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
            <TextBlock Text="Pairing address" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="AddressField" Grid.Column="1"/>
            <TextBlock Grid.Row="1" Text="HTTPS/WSS port" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="PortField" Grid.Row="1" Grid.Column="1"/>
          </Grid>
          <TextBlock Text="The selected private LAN address is advertised for pairing. Changing the port restarts the HTTPS/WSS listener in place and invalidates any open pairing code." TextWrapping="Wrap" Opacity="0.68"/>
          <Button x:Name="Apply" Content="Apply network settings" Style="{StaticResource AccentButtonStyle}" HorizontalAlignment="Right"/>
        </StackPanel>
      </Border>
  </StackPanel>
</Grid>)xaml";

std::uint16_t number_box_port(const controls::NumberBox& field)
{
  const auto value = field.Value();
  return std::isnan(value) || value < 1 || value > 65'535 ? 0 : static_cast<std::uint16_t>(value);
}

template <typename Control>
Control mount_control(const xaml::FrameworkElement& page,
                      std::wstring_view host_name,
                      const Control& control)
{
  find_control<controls::Grid>(page, host_name).Children().Append(control);
  return control;
}

controls::NumberBox number_field(double minimum, double maximum)
{
  const auto field = controls::NumberBox();
  field.Minimum(minimum);
  field.Maximum(maximum);
  field.SpinButtonPlacementMode(controls::NumberBoxSpinButtonPlacementMode::Compact);
  return field;
}
} // namespace

xaml::FrameworkElement make_adapters_page(WinUiPageContext context)
{
  const auto page = load_xaml(adapters_markup);
  const auto& state = context.controller.state();
  auto obs_host = mount_control(page, L"ObsHostField", controls::TextBox());
  auto obs_port = mount_control(page, L"ObsPortField", number_field(1, 65'535));
  auto obs_password = mount_control(page, L"ObsPasswordField", controls::PasswordBox());
  obs_password.PlaceholderText(L"Leave empty to keep the stored password");
  obs_host.Text(wide(state.obs_connection.host));
  obs_port.Value(state.obs_connection.port);
  find_control<controls::Button>(page, L"ApplyObs")
      .Click(
          [context, obs_host, obs_port, obs_password](const auto&, const auto&)
          {
            ManagementObsConnectionRequest request;
            request.host = narrow(obs_host.Text());
            request.port = number_box_port(obs_port);
            if (!obs_password.Password().empty())
              request.password = narrow(obs_password.Password());
            context.controller.apply_obs_connection(std::move(request));
            context.redraw();
          });

  auto vmix_host = mount_control(page, L"VmixHostField", controls::TextBox());
  auto vmix_http_port = mount_control(page, L"VmixHttpPortField", number_field(1, 65'535));
  auto vmix_username = mount_control(page, L"VmixUsernameField", controls::TextBox());
  auto vmix_password = mount_control(page, L"VmixPasswordField", controls::PasswordBox());
  vmix_password.PlaceholderText(L"Keep stored password");
  auto program_output = mount_control(page, L"ProgramOutputField", number_field(1, 4));
  auto program_port = mount_control(page, L"ProgramPortField", number_field(1, 65'535));
  auto preview_output = mount_control(page, L"PreviewOutputField", number_field(1, 4));
  auto preview_port = mount_control(page, L"PreviewPortField", number_field(1, 65'535));
  auto latency = mount_control(page, L"LatencyField", number_field(20, 8'000));
  vmix_host.Text(wide(state.vmix_connection.host));
  vmix_http_port.Value(state.vmix_connection.http_port);
  vmix_username.Text(wide(state.vmix_connection.username));
  program_output.Value(state.vmix_connection.program_output);
  program_port.Value(state.vmix_connection.program_port);
  preview_output.Value(state.vmix_connection.preview_output);
  preview_port.Value(state.vmix_connection.preview_port);
  latency.Value(state.vmix_connection.latency_ms);
  find_control<controls::Button>(page, L"ApplyVmix")
      .Click(
          [context,
           vmix_host,
           vmix_http_port,
           vmix_username,
           vmix_password,
           program_output,
           program_port,
           preview_output,
           preview_port,
           latency](const auto&, const auto&)
          {
            ManagementVmixConnectionRequest request;
            request.host = narrow(vmix_host.Text());
            request.http_port = number_box_port(vmix_http_port);
            request.username = narrow(vmix_username.Text());
            if (!vmix_password.Password().empty())
              request.password = narrow(vmix_password.Password());
            request.program_output = static_cast<std::uint8_t>(program_output.Value());
            request.program_port = number_box_port(program_port);
            request.preview_output = static_cast<std::uint8_t>(preview_output.Value());
            request.preview_port = number_box_port(preview_port);
            request.latency_ms = number_box_port(latency);
            context.controller.apply_vmix_connection(std::move(request));
            context.redraw();
          });
  refresh_adapters_page(page, context);
  return page;
}

xaml::FrameworkElement make_companion_settings_page(WinUiPageContext context)
{
  const auto page = load_xaml(companion_markup);
  const auto& network = context.controller.state().network_settings;
  const auto warning = controls::InfoBar();
  warning.Severity(controls::InfoBarSeverity::Warning);
  warning.IsClosable(false);
  find_control<controls::Grid>(page, L"NetworkWarningHost").Children().Append(warning);
  warning.IsOpen(!network.warning.empty());
  warning.Message(wide(network.warning));
  auto address = mount_control(page, L"AddressField", controls::ComboBox());
  address.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  std::vector<std::pair<std::string, std::string>> choices;
  for (const auto& candidate : network.available_addresses)
    choices.emplace_back(candidate, candidate);
  refresh_choices(address, std::move(choices), network.address);
  auto port = mount_control(page, L"PortField", number_field(1, 65'535));
  port.Value(network.port);
  find_control<controls::Button>(page, L"Apply")
      .Click(
          [context, address, port](const auto&, const auto&)
          {
            ManagementNetworkSettingsRequest request;
            request.address = selected_choice(address);
            request.port = number_box_port(port);
            context.controller.apply_network_settings(std::move(request));
            context.redraw();
          });
  refresh_companion_settings_page(page, context);
  return page;
}

void refresh_adapters_page(const xaml::FrameworkElement& page, WinUiPageContext context)
{
  const auto& state = context.controller.state();
  const auto refresh_visibility = [&](std::wstring_view card_name, std::string_view provider_id)
  {
    const auto installed = std::ranges::any_of(
        state.adapters, [&](const auto& adapter) { return adapter.provider_id == provider_id; });
    find_control<controls::Border>(page, card_name)
        .Visibility(installed ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
  };
  refresh_visibility(L"ObsCard", "obs");
  refresh_visibility(L"VmixCard", "vmix");
  refresh_visibility(L"StreamlabsCard", "streamlabs");
  find_control<controls::StackPanel>(page, L"ObsStatusHost").Children().Clear();
  find_control<controls::StackPanel>(page, L"VmixStatusHost").Children().Clear();
  find_control<controls::StackPanel>(page, L"ObsStatusHost")
      .Children()
      .Append(make_status_badge(state.obs_connection.status.empty() ? "Not configured"
                                                                    : state.obs_connection.status,
                                state.obs_connection.connected ? "healthy" : "warning"));
  find_control<controls::StackPanel>(page, L"VmixStatusHost")
      .Children()
      .Append(make_status_badge(state.vmix_connection.status.empty() ? "Not configured"
                                                                     : state.vmix_connection.status,
                                state.vmix_connection.connected ? "healthy" : "warning"));
}

void refresh_companion_settings_page(const xaml::FrameworkElement& page, WinUiPageContext context)
{
  const auto& network = context.controller.state().network_settings;
  find_control<controls::StackPanel>(page, L"NetworkStatusHost").Children().Clear();
  find_control<controls::StackPanel>(page, L"NetworkStatusHost")
      .Children()
      .Append(make_status_badge(network.listening ? "Listening" : "Unavailable",
                                network.listening ? "healthy" : "error"));
  const auto warning = mounted_control<controls::InfoBar>(page, L"NetworkWarningHost");
  warning.IsOpen(!network.warning.empty());
  warning.Message(wide(network.warning));
  std::vector<std::pair<std::string, std::string>> choices;
  for (const auto& candidate : network.available_addresses)
    choices.emplace_back(candidate, candidate);
  refresh_choices(mounted_control<controls::ComboBox>(page, L"AddressField"), std::move(choices));
}
} // namespace studio_remote::windows
