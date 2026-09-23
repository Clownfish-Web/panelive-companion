#include "winui_management_pages.hpp"

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
constexpr wchar_t status_markup[] = LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RowSpacing="18">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <Grid>
    <StackPanel Spacing="4"><TextBlock Text="Status" FontSize="28" FontWeight="SemiBold"/><TextBlock Text="Review Companion health and control production adapters." Opacity="0.68"/></StackPanel>
    <Button x:Name="Shutdown" Content="Shutdown Companion" HorizontalAlignment="Right" VerticalAlignment="Center"/>
  </Grid>
  <StackPanel Grid.Row="1" Spacing="10">
    <TextBlock Text="Backend adapters" FontSize="18" FontWeight="SemiBold"/>
    <Border Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8">
      <StackPanel x:Name="AdapterRows"/>
    </Border>
  </StackPanel>
  <StackPanel Grid.Row="2" Spacing="10">
    <TextBlock Text="Health feeds" FontSize="18" FontWeight="SemiBold"/>
    <Border Background="#1B212A" BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8">
      <StackPanel x:Name="FeedRows"/>
    </Border>
  </StackPanel>
</Grid>)xaml";

controls::Button command_button(std::wstring_view text)
{
  const auto button = controls::Button();
  button.Content(winrt::box_value(winrt::hstring(text)));
  button.MinWidth(66);
  return button;
}
} // namespace

xaml::FrameworkElement make_status_page(WinUiPageContext context)
{
  const auto page = load_xaml(status_markup);
  const auto& state = context.controller.state();
  auto adapter_rows = find_control<controls::StackPanel>(page, L"AdapterRows");
  for (std::size_t index = 0; index < state.adapters.size(); ++index)
  {
    const auto adapter = state.adapters[index];
    const auto row = controls::Grid();
    row.MinHeight(68);
    row.Padding(xaml::Thickness{14, 8, 14, 8});
    row.ColumnSpacing(12);
    if (index != 0)
    {
      row.Background(xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{18, 255, 255, 255}));
    }
    for (const auto width : {130.0, 105.0, 105.0, 0.0, 220.0})
    {
      controls::ColumnDefinition definition;
      definition.Width(width == 0
                           ? xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star)
                           : xaml::GridLengthHelper::FromPixels(width));
      row.ColumnDefinitions().Append(definition);
    }
    row.Children().Append(make_table_text(adapter.name, false, true));
    auto gateway = make_status_badge(adapter.gateway_state,
                                     adapter.gateway_state == "running" ? "healthy" : "neutral");
    controls::Grid::SetColumn(gateway, 1);
    gateway.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    gateway.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.Children().Append(gateway);
    auto backend = make_status_badge(adapter.backend_state,
                                     adapter.backend_state == "online" ? "healthy" : "warning");
    controls::Grid::SetColumn(backend, 2);
    backend.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    backend.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.Children().Append(backend);
    auto detail = make_table_text(adapter.detail, true);
    detail.TextWrapping(xaml::TextWrapping::Wrap);
    detail.TextTrimming(xaml::TextTrimming::None);
    controls::Grid::SetColumn(detail, 3);
    row.Children().Append(detail);
    const auto actions = controls::StackPanel();
    actions.Orientation(controls::Orientation::Horizontal);
    actions.Spacing(6);
    auto start = command_button(L"Start");
    start.IsEnabled(adapter.can_start);
    start.Click(
        [context, id = adapter.provider_id](const auto&, const auto&)
        {
          context.controller.start_adapter(id);
          context.redraw();
        });
    auto stop = command_button(L"Stop");
    stop.IsEnabled(adapter.can_stop);
    stop.Click(
        [context, id = adapter.provider_id](const auto&, const auto&)
        {
          context.controller.stop_adapter(id);
          context.redraw();
        });
    auto restart = command_button(L"Restart");
    restart.IsEnabled(adapter.can_restart);
    restart.Click(
        [context, id = adapter.provider_id](const auto&, const auto&)
        {
          context.controller.restart_adapter(id);
          context.redraw();
        });
    actions.Children().Append(start);
    actions.Children().Append(stop);
    actions.Children().Append(restart);
    controls::Grid::SetColumn(actions, 4);
    actions.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.Children().Append(actions);
    adapter_rows.Children().Append(row);
  }

  auto feed_rows = find_control<controls::StackPanel>(page, L"FeedRows");
  for (std::size_t index = 0; index < state.status_feeds.size(); ++index)
  {
    const auto& feed = state.status_feeds[index];
    const auto row = controls::Grid();
    row.MinHeight(52);
    row.Padding(xaml::Thickness{14, 8, 14, 8});
    row.ColumnSpacing(12);
    controls::ColumnDefinition name_column;
    name_column.Width(xaml::GridLengthHelper::FromPixels(210));
    controls::ColumnDefinition state_column;
    state_column.Width(xaml::GridLengthHelper::FromPixels(110));
    controls::ColumnDefinition detail_column;
    detail_column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
    row.ColumnDefinitions().Append(name_column);
    row.ColumnDefinitions().Append(state_column);
    row.ColumnDefinitions().Append(detail_column);
    auto name = make_table_text(feed.name, false, true);
    name.TextWrapping(xaml::TextWrapping::Wrap);
    name.TextTrimming(xaml::TextTrimming::None);
    row.Children().Append(name);
    auto badge = make_status_badge(feed.state,
                                   feed.state == "healthy"    ? "healthy"
                                   : feed.state == "degraded" ? "warning"
                                                              : "neutral");
    controls::Grid::SetColumn(badge, 1);
    badge.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    badge.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.Children().Append(badge);
    auto detail = make_table_text(feed.detail, true);
    detail.TextWrapping(xaml::TextWrapping::Wrap);
    detail.TextTrimming(xaml::TextTrimming::None);
    controls::Grid::SetColumn(detail, 2);
    row.Children().Append(detail);
    feed_rows.Children().Append(row);
  }

  find_control<controls::Button>(page, L"Shutdown")
      .Click(
          [context](const auto&, const auto&)
          {
            context.confirm(L"Shut down Companion",
                            L"Shut down PaneLive Companion and disconnect all mobile clients?",
                            [context] { context.controller.shutdown(); });
          });
  return page;
}
} // namespace studio_remote::windows
