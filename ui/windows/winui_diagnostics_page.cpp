#include "winui_management_pages.hpp"

#include <sstream>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
constexpr wchar_t diagnostics_markup[] = LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RowSpacing="18">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <StackPanel Spacing="4"><TextBlock Text="Diagnostics" FontSize="28" FontWeight="SemiBold"/><TextBlock Text="Run local audio checks." Opacity="0.68"/></StackPanel>
  <StackPanel Grid.Row="1" Spacing="18">
      <Border Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8" Padding="18">
        <StackPanel Spacing="14">
          <TextBlock Text="Local audio diagnostics" FontSize="18" FontWeight="SemiBold"/>
          <Border Height="1" Background="#343E4C"/>
          <Grid ColumnSpacing="10"><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions><ComboBox x:Name="Output" HorizontalAlignment="Stretch"/><Button x:Name="RefreshOutputs" Grid.Column="1" Content="Refresh devices"/></Grid>
          <Grid ColumnSpacing="12"><Grid.ColumnDefinitions><ColumnDefinition Width="80"/><ColumnDefinition Width="*"/><ColumnDefinition Width="70"/></Grid.ColumnDefinitions><TextBlock Text="Gain" VerticalAlignment="Center" Opacity="0.68"/><Slider x:Name="Gain" Grid.Column="1" Minimum="-24" Maximum="12" StepFrequency="1"/><TextBlock x:Name="GainValue" Grid.Column="2" VerticalAlignment="Center"/></Grid>
          <StackPanel Orientation="Horizontal" Spacing="8"><Button x:Name="PreOpus" Content="Play pre-Opus IPC"/><Button x:Name="PostOpus" Content="Play post-Opus"/><Button x:Name="Stop" Content="Stop"/><Button x:Name="DebugLog" Content="Begin debug log"/><Button x:Name="SaveReport" Content="Save report"/></StackPanel>
          <Grid ColumnSpacing="12" RowSpacing="8"><Grid.ColumnDefinitions><ColumnDefinition Width="130"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions><TextBlock Text="Maximum peak" Opacity="0.68"/><Grid x:Name="MaximumPeakHost" Grid.Column="1"/><TextBlock Grid.Row="1" Text="Average peak" Opacity="0.68"/><Grid x:Name="AveragePeakHost" Grid.Row="1" Grid.Column="1"/></Grid>
          <TextBlock x:Name="AudioStatus" TextWrapping="Wrap" Opacity="0.68"/>
        </StackPanel>
      </Border>
  </StackPanel>
</Grid>)xaml";

std::string audio_summary(const ManagementAudioDiagnosticStatus& status)
{
  if (!status.error.empty())
    return status.error;
  if (!status.playing)
    return "Audio diagnostics are idle.";
  return "Playing on " + status.device_name + " - received " +
         std::to_string(status.received_frames) + " frames, played " +
         std::to_string(status.played_frames) + ".";
}
} // namespace

xaml::FrameworkElement make_diagnostics_page(WinUiPageContext context)
{
  const auto page = load_xaml(diagnostics_markup);
  const auto& state = context.controller.state();
  auto output = find_control<controls::ComboBox>(page, L"Output");
  std::vector<std::pair<std::string, std::string>> choices;
  std::string selected_output;
  for (const auto& device : state.audio_outputs)
  {
    choices.emplace_back(device.id, device.name);
    if (device.is_default || selected_output.empty())
      selected_output = device.id;
  }
  refresh_choices(output, std::move(choices), selected_output);
  auto gain = find_control<controls::Slider>(page, L"Gain");
  auto gain_value = find_control<controls::TextBlock>(page, L"GainValue");
  gain.Value(state.audio_status.gain_db);
  gain_value.Text(std::to_wstring(static_cast<int>(state.audio_status.gain_db)) + L" dB");
  gain.ValueChanged(
      [context, gain_value](const auto&,
                            const controls::Primitives::RangeBaseValueChangedEventArgs& event)
      {
        gain_value.Text(std::to_wstring(static_cast<int>(event.NewValue())) + L" dB");
        context.controller.set_audio_gain(static_cast<float>(event.NewValue()));
      });
  find_control<controls::Button>(page, L"RefreshOutputs")
      .Click(
          [context](const auto&, const auto&)
          {
            context.controller.refresh();
            context.redraw();
          });
  const auto start = [context, output](ManagementAudioDiagnosticTap tap)
  {
    const auto selected = selected_choice(output);
    if (selected.empty())
      return;
    context.controller.start_audio(selected, tap);
    context.redraw();
  };
  find_control<controls::Button>(page, L"PreOpus")
      .Click([start](const auto&, const auto&) { start(ManagementAudioDiagnosticTap::pre_opus); });
  find_control<controls::Button>(page, L"PostOpus")
      .Click([start](const auto&, const auto&) { start(ManagementAudioDiagnosticTap::post_opus); });
  auto stop = find_control<controls::Button>(page, L"Stop");
  stop.IsEnabled(state.audio_status.playing);
  stop.Click(
      [context](const auto&, const auto&)
      {
        context.controller.stop_audio();
        context.redraw();
      });
  auto debug = find_control<controls::Button>(page, L"DebugLog");
  debug.Content(winrt::box_value(state.audio_status.debug_log_active ? L"Complete debug log"
                                                                     : L"Begin debug log"));
  debug.Click(
      [context](const auto&, const auto&)
      {
        if (context.controller.state().audio_status.debug_log_active)
          context.controller.complete_audio_debug_log();
        else
          context.controller.begin_audio_debug_log();
        context.redraw();
      });
  auto save = find_control<controls::Button>(page, L"SaveReport");
  save.IsEnabled(state.audio_status.debug_log_available);
  save.Click(
      [context, weak_page = winrt::make_weak(page)](const auto&, const auto&)
      {
        const auto page = weak_page.get();
        if (!page)
          return;
        save_text_file(page.XamlRoot(),
                       "panelive-audio-diagnostics.jsonl",
                       context.controller.audio_debug_report());
      });
  const auto maximum_peak = controls::ProgressBar();
  maximum_peak.Minimum(0);
  maximum_peak.Maximum(1);
  maximum_peak.Value(state.audio_status.maximum_peak_last_second);
  find_control<controls::Grid>(page, L"MaximumPeakHost").Children().Append(maximum_peak);
  const auto average_peak = controls::ProgressBar();
  average_peak.Minimum(0);
  average_peak.Maximum(1);
  average_peak.Value(state.audio_status.average_peak_last_second);
  find_control<controls::Grid>(page, L"AveragePeakHost").Children().Append(average_peak);
  find_control<controls::TextBlock>(page, L"AudioStatus")
      .Text(wide(audio_summary(state.audio_status)));
  return page;
}

void refresh_diagnostics_page(const xaml::FrameworkElement& page, WinUiPageContext context)
{
  const auto& state = context.controller.state();
  std::vector<std::pair<std::string, std::string>> choices;
  for (const auto& device : state.audio_outputs)
    choices.emplace_back(device.id, device.name);
  refresh_choices(find_control<controls::ComboBox>(page, L"Output"), std::move(choices));
  find_control<controls::Button>(page, L"Stop").IsEnabled(state.audio_status.playing);
  find_control<controls::Button>(page, L"DebugLog")
      .Content(winrt::box_value(state.audio_status.debug_log_active ? L"Complete debug log"
                                                                    : L"Begin debug log"));
  find_control<controls::Button>(page, L"SaveReport")
      .IsEnabled(state.audio_status.debug_log_available);
  mounted_control<controls::ProgressBar>(page, L"MaximumPeakHost")
      .Value(state.audio_status.maximum_peak_last_second);
  mounted_control<controls::ProgressBar>(page, L"AveragePeakHost")
      .Value(state.audio_status.average_peak_last_second);
  find_control<controls::TextBlock>(page, L"AudioStatus")
      .Text(wide(audio_summary(state.audio_status)));
}
} // namespace studio_remote::windows
