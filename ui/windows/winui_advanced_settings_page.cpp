#include "winui_management_pages.hpp"

#include <cmath>
#include <sstream>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
constexpr wchar_t advanced_markup[] = LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RowSpacing="18">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel Spacing="4"><TextBlock Text="Advanced Settings" FontSize="28" FontWeight="SemiBold"/><TextBlock Text="Choose the interface and configure Companion-wide behavior." Opacity="0.68"/></StackPanel>
  <StackPanel Grid.Row="1" Spacing="18">
      <Border Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="12">
          <TextBlock Text="Interface" FontSize="18" FontWeight="SemiBold"/>
          <Border Height="1" Background="#343E4C"/>
          <Grid ColumnSpacing="24"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="180"/></Grid.ColumnDefinitions><TextBlock Text="Choose which day-to-day and advanced controls are visible." TextWrapping="Wrap" VerticalAlignment="Center"/><ComboBox x:Name="InterfaceMode" Grid.Column="1" VerticalAlignment="Center" HorizontalAlignment="Stretch"><ComboBoxItem Content="Basic"/><ComboBoxItem Content="Advanced"/></ComboBox></Grid>
          <TextBlock Text="Advanced mode reveals network, diagnostics, audio, and installation settings." TextWrapping="Wrap" Opacity="0.68"/>
        </StackPanel>
      </Border>
      <Border Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="12">
          <TextBlock Text="Access control" FontSize="18" FontWeight="SemiBold"/>
          <Border Height="1" Background="#343E4C"/>
          <Grid ColumnSpacing="24"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><TextBlock Text="Limit each production backend to one operator client. Other paired clients remain viewers." TextWrapping="Wrap" VerticalAlignment="Center"/><ToggleSwitch x:Name="SingleOperator" Grid.Column="1" OnContent="Enabled" OffContent="Disabled" VerticalAlignment="Center"/></Grid>
        </StackPanel>
      </Border>
      <Border Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="12">
          <TextBlock Text="Background operation" FontSize="18" FontWeight="SemiBold"/>
          <Border Height="1" Background="#343E4C"/>
          <Grid ColumnSpacing="24"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><TextBlock Text="Start PaneLive Companion automatically when this user signs in." TextWrapping="Wrap" VerticalAlignment="Center"/><ToggleSwitch x:Name="SignInStartup" Grid.Column="1" OnContent="Enabled" OffContent="Disabled" VerticalAlignment="Center"/></Grid>
        </StackPanel>
      </Border>
      <Border x:Name="DebugCard" Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="12">
          <TextBlock Text="Debug diagnostics" FontSize="18" FontWeight="SemiBold"/>
          <Border Height="1" Background="#343E4C"/>
          <Grid ColumnSpacing="24"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><StackPanel Spacing="3"><TextBlock Text="Record debug diagnostics"/><TextBlock x:Name="EnduranceState" Opacity="0.68"/></StackPanel><ToggleSwitch x:Name="EnduranceToggle" Grid.Column="1" HorizontalAlignment="Right" VerticalAlignment="Center" OnContent="Enabled" OffContent="Disabled"/></Grid>
          <TextBlock x:Name="EnduranceSummary" TextWrapping="Wrap" Opacity="0.68"/>
          <TextBlock Text="Default off. Keeps at most 24 hours and 64 MiB. Stops after 24 hours or restart." TextWrapping="Wrap" Opacity="0.68"/>
          <Button x:Name="ClearDebugLogs" Content="Clear debug logs" HorizontalAlignment="Right"/>
          <Grid x:Name="EnduranceErrorHost"/>
        </StackPanel>
      </Border>
      <Border x:Name="AudioCard" Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="14">
          <StackPanel Spacing="4"><TextBlock Text="Audio" FontSize="17" FontWeight="SemiBold"/><TextBlock x:Name="AudioDescription" Text="Configure mixing headroom and the Preview-only source audition bus." TextWrapping="Wrap" Opacity="0.68"/></StackPanel>
          <Border Height="1" Background="#343E4C"/>
          <Grid x:Name="AutomaticHeadroomRow" ColumnSpacing="24" RowSpacing="3"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions><TextBlock Text="Automatic summing headroom" FontWeight="SemiBold" VerticalAlignment="Center"/><ToggleSwitch x:Name="AutomaticHeadroom" Grid.Column="1" OnContent="Enabled" OffContent="Disabled" VerticalAlignment="Center"/><TextBlock Grid.Row="1" Text="Reduce each additional monitored source to protect the combined mix from clipping." TextWrapping="Wrap" Opacity="0.68"/></Grid>
          <Grid x:Name="HeadroomRow" ColumnSpacing="12"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="100"/></Grid.ColumnDefinitions><TextBlock Text="Attenuation per additional source" VerticalAlignment="Center"/><Grid x:Name="HeadroomField" Grid.Column="1"/></Grid>
          <Grid ColumnSpacing="24" RowSpacing="3"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions><TextBlock Text="Audio source audition bus" FontWeight="SemiBold" VerticalAlignment="Center"/><ToggleSwitch x:Name="AuditionBus" Grid.Column="1" OnContent="Enabled" OffContent="Disabled" VerticalAlignment="Center"/><TextBlock Grid.Row="1" Text="Hear audio from monitored OBS sources in Preview before they go live. They stay out of Program and the broadcast mix." TextWrapping="Wrap" Opacity="0.68"/></Grid>
          <Button x:Name="ApplyAudio" Content="Apply audio settings" Style="{StaticResource AccentButtonStyle}" HorizontalAlignment="Right"/>
        </StackPanel>
      </Border>
      <Border x:Name="LocationsCard" Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="12">
          <TextBlock Text="Production software locations" FontSize="18" FontWeight="SemiBold"/>
          <Border Height="1" Background="#343E4C"/>
          <Grid ColumnSpacing="10"><Grid.ColumnDefinitions><ColumnDefinition Width="140"/><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><TextBlock Text="vMix directory" Opacity="0.68" VerticalAlignment="Center"/><Grid x:Name="VmixDirectoryField" Grid.Column="1"/><Button x:Name="BrowseVmix" Grid.Column="2" Content="Browse"/></Grid>
          <TextBlock x:Name="VmixDirectoryStatus" TextWrapping="Wrap" Opacity="0.68"/>
          <Grid ColumnSpacing="10"><Grid.ColumnDefinitions><ColumnDefinition Width="140"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><TextBlock Text="OBS directory" Opacity="0.68"/><TextBlock x:Name="ObsDirectory" Grid.Column="1" TextTrimming="CharacterEllipsis"/></Grid>
          <Button x:Name="ApplyVmixDirectory" Content="Apply vMix directory" Style="{StaticResource AccentButtonStyle}" HorizontalAlignment="Right"/>
        </StackPanel>
      </Border>
  </StackPanel>
</Grid>)xaml";

std::string endurance_summary(const ManagementEnduranceStatus& status)
{
  std::ostringstream output;
  if (!status.file.empty())
    output << "File: " << status.file << "\n";
  output << "Records: " << status.records_written << "  Bytes: " << status.bytes_written
         << "  Retained bytes: " << status.retained_bytes
         << "  Queued bytes: " << status.queued_bytes << "  Queued: " << status.queued_events
         << "  Dropped: " << status.dropped_events;
  return output.str();
}

std::string location_summary(const ManagementAdvancedSettings& settings)
{
  if (!settings.configured_vmix_directory.empty())
    return "Configured override: " + settings.configured_vmix_directory +
           ". The change applies fully after the next Companion start.";
  if (!settings.detected_vmix_directory.empty())
    return "Detected automatically: " + settings.detected_vmix_directory;
  return "vMix was not detected automatically.";
}
} // namespace

xaml::FrameworkElement make_advanced_settings_page(WinUiPageContext context)
{
  const auto page = load_xaml(advanced_markup);
  const auto& state = context.controller.state();
  const bool advanced = state.interface_mode == ManagementInterfaceMode::advanced;
  auto mode = find_control<controls::ComboBox>(page, L"InterfaceMode");
  mode.SelectedIndex(advanced ? 1 : 0);
  mode.SelectionChanged(
      [context](const auto& sender, const auto&)
      {
        const auto selected = sender.template as<controls::ComboBox>().SelectedIndex() == 1
                                  ? ManagementInterfaceMode::advanced
                                  : ManagementInterfaceMode::basic;
        if (selected == context.controller.state().interface_mode)
          return;
        context.controller.set_interface_mode(selected);
        context.redraw();
      });

  auto single_operator = find_control<controls::ToggleSwitch>(page, L"SingleOperator");
  single_operator.IsOn(state.basic_settings.single_operator_mode);
  single_operator.Toggled(
      [context](const auto& sender, const auto&)
      {
        const auto single_operator = sender.template as<controls::ToggleSwitch>();
        const bool original = context.controller.state().basic_settings.single_operator_mode;
        const bool enabled = single_operator.IsOn();
        if (enabled && !original)
        {
          single_operator.IsOn(false);
          context.confirm(
              L"Enable one operator per production backend",
              L"For each backend with multiple operators, the most recently active operator "
              L"will remain and every other operator will immediately become a viewer.",
              [context]
              {
                context.controller.set_single_operator_mode(true);
                context.redraw();
              });
          return;
        }
        if (enabled != original)
        {
          context.controller.set_single_operator_mode(enabled);
          context.redraw();
        }
      });
  auto startup = find_control<controls::ToggleSwitch>(page, L"SignInStartup");
  startup.IsOn(state.basic_settings.sign_in_startup);
  startup.Toggled(
      [context](const auto& sender, const auto&)
      {
        const auto startup = sender.template as<controls::ToggleSwitch>();
        if (startup.IsOn() == context.controller.state().basic_settings.sign_in_startup)
          return;
        context.controller.set_sign_in_startup(startup.IsOn());
        context.redraw();
      });

  auto debug_toggle = find_control<controls::ToggleSwitch>(page, L"EnduranceToggle");
  debug_toggle.IsOn(state.endurance_status.enabled);
  const auto debug_error = controls::InfoBar();
  debug_error.Severity(controls::InfoBarSeverity::Error);
  debug_error.IsClosable(false);
  find_control<controls::Grid>(page, L"EnduranceErrorHost").Children().Append(debug_error);
  debug_toggle.Toggled(
      [context](const auto& sender, const auto&)
      {
        const bool enabled = sender.template as<controls::ToggleSwitch>().IsOn();
        if (enabled != context.controller.state().endurance_status.enabled)
        {
          context.controller.set_endurance_logging(enabled);
          context.redraw();
        }
      });
  find_control<controls::Button>(page, L"ClearDebugLogs")
      .Click(
          [context](const auto&, const auto&)
          {
            context.controller.clear_debug_logs();
            context.redraw();
          });

  const auto& settings = state.advanced_settings;
  auto automatic_headroom = find_control<controls::ToggleSwitch>(page, L"AutomaticHeadroom");
  const auto headroom = controls::NumberBox();
  headroom.Minimum(1);
  headroom.Maximum(4);
  headroom.SpinButtonPlacementMode(controls::NumberBoxSpinButtonPlacementMode::Compact);
  find_control<controls::Grid>(page, L"HeadroomField").Children().Append(headroom);
  auto audition_bus = find_control<controls::ToggleSwitch>(page, L"AuditionBus");
  automatic_headroom.IsOn(settings.automatic_headroom);
  headroom.Value(settings.headroom_per_additional_source_db);
  headroom.IsEnabled(settings.automatic_headroom);
  audition_bus.IsOn(settings.audio_source_audition_bus);
  automatic_headroom.Toggled(
      [headroom](const auto& sender, const auto&)
      { headroom.IsEnabled(sender.template as<controls::ToggleSwitch>().IsOn()); });
  auto apply_audio = find_control<controls::Button>(page, L"ApplyAudio");
  apply_audio.Click(
      [context, automatic_headroom, headroom, audition_bus](const auto&, const auto&)
      {
        ManagementAudioMixSettingsRequest request;
        request.automatic_headroom = automatic_headroom.IsOn();
        request.headroom_per_additional_source_db =
            std::isnan(headroom.Value()) ? 2U : static_cast<std::uint32_t>(headroom.Value());
        request.audio_source_audition_bus = audition_bus.IsOn();
        const auto apply = [context, request]
        {
          context.controller.apply_audio_mix(request);
          context.redraw();
        };
        if (request.audio_source_audition_bus &&
            !context.controller.state().advanced_settings.audio_source_audition_bus)
        {
          context.confirm(L"Audio source audition bus",
                          L"Monitored Preview-only sources will remain active and may be audible "
                          L"through the OBS monitoring device. They are not inserted into Program.",
                          apply);
        }
        else
        {
          apply();
        }
      });

  const auto vmix_directory = controls::TextBox();
  find_control<controls::Grid>(page, L"VmixDirectoryField").Children().Append(vmix_directory);
  vmix_directory.Text(wide(settings.configured_vmix_directory));
  find_control<controls::TextBlock>(page, L"VmixDirectoryStatus")
      .Text(wide(location_summary(settings)));
  find_control<controls::TextBlock>(page, L"ObsDirectory").Text(wide(settings.obs_directory));
  find_control<controls::Button>(page, L"BrowseVmix")
      .Click(
          [weak_page = winrt::make_weak(page), vmix_directory](const auto&, const auto&)
          {
            const auto page = weak_page.get();
            if (!page)
              return;
            if (const auto selected =
                    choose_directory(page.XamlRoot(), narrow(vmix_directory.Text())))
              vmix_directory.Text(wide(*selected));
          });
  find_control<controls::Button>(page, L"ApplyVmixDirectory")
      .Click(
          [context, vmix_directory](const auto&, const auto&)
          {
            context.controller.apply_vmix_directory(narrow(vmix_directory.Text()));
            context.redraw();
          });
  refresh_advanced_settings_page(page, context);
  return page;
}

void refresh_advanced_settings_page(const xaml::FrameworkElement& page, WinUiPageContext context)
{
  const auto& state = context.controller.state();
  const bool advanced = state.interface_mode == ManagementInterfaceMode::advanced;
  const auto visibility = advanced ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
  const auto mode = find_control<controls::ComboBox>(page, L"InterfaceMode");
  if (mode.SelectedIndex() != (advanced ? 1 : 0))
    mode.SelectedIndex(advanced ? 1 : 0);
  const auto single_operator = find_control<controls::ToggleSwitch>(page, L"SingleOperator");
  if (single_operator.IsOn() != state.basic_settings.single_operator_mode)
    single_operator.IsOn(state.basic_settings.single_operator_mode);
  const auto startup = find_control<controls::ToggleSwitch>(page, L"SignInStartup");
  if (startup.IsOn() != state.basic_settings.sign_in_startup)
    startup.IsOn(state.basic_settings.sign_in_startup);
  const auto& endurance = state.endurance_status;
  const auto toggle = find_control<controls::ToggleSwitch>(page, L"EnduranceToggle");
  if (toggle.IsOn() != endurance.enabled)
    toggle.IsOn(endurance.enabled);
  find_control<controls::TextBlock>(page, L"EnduranceState")
      .Text(endurance.enabled ? L"Recording debug diagnostics" : L"Disabled");
  find_control<controls::TextBlock>(page, L"EnduranceSummary")
      .Text(wide(endurance_summary(endurance)));
  const auto error = mounted_control<controls::InfoBar>(page, L"EnduranceErrorHost");
  error.IsOpen(!endurance.last_error.empty());
  error.Message(wide(endurance.last_error));
  find_control<controls::Border>(page, L"DebugCard").Visibility(visibility);
  find_control<controls::Border>(page, L"LocationsCard").Visibility(visibility);
  find_control<controls::Grid>(page, L"AutomaticHeadroomRow").Visibility(visibility);
  find_control<controls::Grid>(page, L"HeadroomRow").Visibility(visibility);
  find_control<controls::TextBlock>(page, L"AudioDescription")
      .Text(
          advanced
              ? L"Configure mixing headroom and the Preview-only source audition bus."
              : L"Control whether monitored Preview sources stay active for private auditioning.");
  find_control<controls::Button>(page, L"ApplyAudio")
      .Content(winrt::box_value(advanced ? L"Apply audio settings" : L"Apply audio setting"));
  find_control<controls::TextBlock>(page, L"VmixDirectoryStatus")
      .Text(wide(location_summary(state.advanced_settings)));
  find_control<controls::TextBlock>(page, L"ObsDirectory")
      .Text(wide(state.advanced_settings.obs_directory));
}
} // namespace studio_remote::windows
