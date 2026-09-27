#include "winui_management_pages.hpp"
#include "winui_pairing_preview.hpp"

#include "../shared/pairing_preview.hpp"

#include <algorithm>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
constexpr wchar_t clients_markup[] = LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RowSpacing="18">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <Grid ColumnSpacing="12">
    <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
    <StackPanel Spacing="4">
      <TextBlock Text="Clients" FontSize="28" FontWeight="SemiBold"/>
      <TextBlock Text="Pair devices and manage access to production backends." Opacity="0.68"/>
    </StackPanel>
    <StackPanel Grid.Column="1" Orientation="Horizontal" Spacing="8">
      <Button x:Name="IdentifyAllButton" Content="Identify all" VerticalAlignment="Center"/>
      <Button x:Name="RefreshButton" Content="Refresh" VerticalAlignment="Center"/>
    </StackPanel>
    <Button x:Name="PairButton" Grid.Column="2" Content="Pair new client" Style="{StaticResource AccentButtonStyle}" VerticalAlignment="Center"/>
  </Grid>
  <StackPanel x:Name="PairingHost" Grid.Row="1"/>
  <Border Grid.Row="2" Background="#1B212A"
          BorderBrush="#343E4C" BorderThickness="1" CornerRadius="8">
    <Grid>
      <Grid.RowDefinitions><RowDefinition Height="44"/><RowDefinition Height="*"/></Grid.RowDefinitions>
      <Grid Padding="14,0" ColumnSpacing="12">
        <Grid.ColumnDefinitions>
          <ColumnDefinition Width="90"/><ColumnDefinition Width="2*"/><ColumnDefinition Width="110"/>
          <ColumnDefinition Width="90"/><ColumnDefinition Width="140"/><ColumnDefinition Width="210"/>
        </Grid.ColumnDefinitions>
        <Button x:Name="SortStatus" Padding="0" Background="Transparent" BorderThickness="0" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"/>
        <Button Grid.Column="1" x:Name="SortClient" Padding="0" Background="Transparent" BorderThickness="0" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"/>
        <Button Grid.Column="2" x:Name="SortBackend" Padding="0" Background="Transparent" BorderThickness="0" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"/>
        <Button Grid.Column="3" x:Name="SortRole" Padding="0" Background="Transparent" BorderThickness="0" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"/>
        <Button Grid.Column="4" x:Name="SortLastSeen" Padding="0" Background="Transparent" BorderThickness="0" HorizontalAlignment="Stretch" HorizontalContentAlignment="Left"/>
        <TextBlock Grid.Column="5" Text="Actions" FontWeight="SemiBold" Opacity="0.68" VerticalAlignment="Center"/>
      </Grid>
      <StackPanel x:Name="ClientRows" Grid.Row="1"/>
    </Grid>
  </Border>
</Grid>)xaml";

controls::Button action_button(wchar_t glyph, std::wstring_view tooltip)
{
  const auto button = controls::Button();
  button.Width(36);
  button.Height(34);
  button.Padding(xaml::Thickness{});
  const auto icon = controls::FontIcon();
  icon.FontFamily(management_icon_font());
  const wchar_t glyph_text[]{glyph, L'\0'};
  icon.Glyph(glyph_text);
  button.Content(icon);
  controls::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring(tooltip)));
  xaml::Automation::AutomationProperties::SetName(button, winrt::hstring(tooltip));
  return button;
}

void bind_sort_heading(const xaml::FrameworkElement& page,
                       WinUiPageContext context,
                       std::wstring_view control,
                       std::wstring_view title,
                       ManagementClientColumn column)
{
  const auto sort = context.controller.state().client_sort;
  const auto active = sort.column == column;
  const auto ascending = sort.direction == ManagementSortDirection::ascending;
  auto button = find_control<controls::Button>(page, control);
  button.Content(winrt::box_value(winrt::hstring(
      std::wstring(title) + (active ? (ascending ? L" \u2191" : L" \u2193") : L" \u2195"))));
  const auto label =
      std::wstring(title) +
      (active ? (ascending ? L", sorted ascending" : L", sorted descending") : L", sortable");
  xaml::Automation::AutomationProperties::SetName(button, winrt::hstring(label));
  controls::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring(label)));
  button.Click(
      [context, column](const auto&, const auto&)
      {
        context.controller.sort_clients(column);
        context.redraw();
      });
}

controls::Button teleprompter_action(const ManagementClientRow& client)
{
  const bool attached = client.attached_script_count.value_or(0) > 0;
  const auto detail = client.attached_script_count
                          ? std::to_string(*client.attached_script_count) + " attached scripts"
                          : "attachments unavailable";
  auto button = action_button(L'\xE8A5', L"Teleprompter - " + wide(detail));
  if (attached)
  {
    const auto green = xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 87, 214, 141});
    button.Foreground(green);
    controls::Grid layers;
    auto icon = button.Content().as<controls::FontIcon>();
    button.Content(nullptr);
    layers.Children().Append(icon);
    controls::Border dot;
    dot.Width(7);
    dot.Height(7);
    dot.CornerRadius(xaml::CornerRadius{4});
    dot.Background(green);
    dot.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    dot.VerticalAlignment(xaml::VerticalAlignment::Bottom);
    layers.Children().Append(dot);
    button.Content(layers);
  }
  return button;
}

controls::Button remote_tally_action(WinUiPageContext context, const ManagementClientRow& client)
{
  const bool allowed = client.remote_tally_allowed.value();
  const auto title = allowed ? L"Withdraw web tally permission" : L"Allow web tallies";
  auto button = action_button(L'\xE774', title);
  button.Name(L"RemoteTallyPermissionButton");
  if (allowed)
    button.Foreground(xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 87, 214, 141}));
  button.Click(
      [context, client, allowed, title](const auto&, const auto&)
      {
        const auto detail = allowed
                                ? L"Withdraw web tally permission for '" + wide(client.name) +
                                      L"'? Its published links will be retired. Removal stays "
                                      L"pending while the remote service is offline."
                                : L"Allow '" + wide(client.name) +
                                      L"' to publish web tally lights? Anyone with a shared link "
                                      L"can read its selected names and statuses. This permission "
                                      L"does not change production control access.";
        context.confirm(title,
                        detail,
                        [context, id = client.id, allowed]
                        {
                          if (allowed)
                            context.controller.withdraw_remote_tally(id);
                          else
                            context.controller.allow_remote_tally(id);
                          context.redraw();
                        });
      });
  return button;
}

wchar_t device_glyph(DeviceType type)
{
  switch (type)
  {
  case DeviceType::smartphone:
    return L'\xE8EA';
  case DeviceType::tablet:
    return L'\xE70A';
  case DeviceType::unknown:
    return L'\xE772';
  }
  return L'\xE772';
}

controls::Button operator_action_button(DeviceType type)
{
  auto button = action_button(device_glyph(type), L"Promote to operator");
  const auto icon_layers = controls::Grid();
  const auto device_icon = controls::FontIcon();
  device_icon.FontFamily(management_icon_font());
  const wchar_t glyph_text[]{device_glyph(type), L'\0'};
  device_icon.Glyph(glyph_text);
  device_icon.FontSize(16);
  icon_layers.Children().Append(device_icon);
  const auto write_badge = controls::FontIcon();
  write_badge.FontFamily(management_icon_font());
  write_badge.Glyph(L"\xE70F");
  write_badge.FontSize(9);
  write_badge.HorizontalAlignment(xaml::HorizontalAlignment::Right);
  write_badge.VerticalAlignment(xaml::VerticalAlignment::Bottom);
  write_badge.Margin(xaml::Thickness{11, 11, 0, 0});
  icon_layers.Children().Append(write_badge);
  button.Content(icon_layers);
  return button;
}

xaml::FrameworkElement make_pairing_card(const ManagementPairing& pairing,
                                         const std::function<void()>& enlarge)
{
  const auto card = load_xaml(LR"xaml(
<Border xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Background="#1B212A"
        BorderBrush="#4F9CF9" BorderThickness="1"
        CornerRadius="8" Padding="16">
  <Grid ColumnSpacing="20">
    <Grid.ColumnDefinitions><ColumnDefinition Width="180"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
    <Viewbox x:Name="QrHost" Width="180" Height="180"/>
    <StackPanel x:Name="EnlargeHost" Grid.Column="1" VerticalAlignment="Center"/>
    <StackPanel Grid.Column="2" VerticalAlignment="Center" Spacing="8">
      <TextBlock Text="Pair a new client" FontSize="18" FontWeight="SemiBold"/>
      <TextBlock Text="Scan this single-use code with the PaneLive mobile app. It is cleared automatically when used or expired."
                 TextWrapping="Wrap" Opacity="0.68"/>
      <TextBlock x:Name="ExpiryText" Foreground="#F2B84B"/>
    </StackPanel>
  </Grid>
</Border>)xaml");
  find_control<controls::Viewbox>(card, L"QrHost")
      .Child(
          make_pairing_image(make_pairing_qr_image(pairing.payload), PairingPreview::normal_size));
  auto enlarge_button = action_button(L'\xE721', L"Enlarge pairing QR for 30 seconds");
  enlarge_button.Click(
      [enlarge](const auto&, const auto&)
      {
        if (enlarge)
          enlarge();
      });
  find_control<controls::StackPanel>(card, L"EnlargeHost").Children().Append(enlarge_button);
  find_control<controls::TextBlock>(card, L"ExpiryText")
      .Text(L"This pairing code expires automatically and can be used only once.");
  return card;
}
} // namespace

xaml::FrameworkElement make_clients_page(WinUiPageContext context)
{
  if (!context.controller.teleprompter().state().device_id.empty())
    return make_teleprompter_page(context);
  const auto page = load_xaml(clients_markup);
  const auto& state = context.controller.state();
  bind_sort_heading(page, context, L"SortStatus", L"Status", ManagementClientColumn::status);
  bind_sort_heading(page, context, L"SortClient", L"Client", ManagementClientColumn::client);
  bind_sort_heading(page, context, L"SortBackend", L"Backend", ManagementClientColumn::backend);
  bind_sort_heading(page, context, L"SortRole", L"Role", ManagementClientColumn::role);
  bind_sort_heading(
      page, context, L"SortLastSeen", L"Last seen", ManagementClientColumn::last_seen);
  auto pairing_host = find_control<controls::StackPanel>(page, L"PairingHost");
  if (state.pairing)
    pairing_host.Children().Append(make_pairing_card(*state.pairing, context.enlarge_pairing));

  auto rows = find_control<controls::StackPanel>(page, L"ClientRows");
  for (std::size_t index = 0; index < state.clients.size(); ++index)
  {
    const auto client = state.clients[index];
    const auto row = controls::Grid();
    row.MinHeight(56);
    row.Padding(xaml::Thickness{14, 0, 14, 0});
    row.ColumnSpacing(12);
    if (index % 2 == 0)
      row.Background(xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{28, 255, 255, 255}));
    for (const auto width : {90.0, 0.0, 110.0, 90.0, 140.0, 210.0})
    {
      controls::ColumnDefinition definition;
      definition.Width(width == 0.0
                           ? xaml::GridLengthHelper::FromValueAndType(2, xaml::GridUnitType::Star)
                           : xaml::GridLengthHelper::FromPixels(width));
      row.ColumnDefinitions().Append(definition);
    }

    auto status = make_status_badge(client.connected ? "Online" : "Offline",
                                    client.connected ? "healthy" : "neutral");
    status.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    status.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.Children().Append(status);

    const auto client_identity = controls::Grid();
    client_identity.ColumnSpacing(8);
    client_identity.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::ColumnDefinition icon_column;
    icon_column.Width(xaml::GridLengthHelper::Auto());
    controls::ColumnDefinition name_column;
    name_column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
    client_identity.ColumnDefinitions().Append(icon_column);
    client_identity.ColumnDefinitions().Append(name_column);
    const auto device_icon = controls::FontIcon();
    device_icon.FontFamily(management_icon_font());
    const wchar_t glyph_text[]{device_glyph(client.device_type), L'\0'};
    device_icon.Glyph(glyph_text);
    device_icon.FontSize(16);
    client_identity.Children().Append(device_icon);
    const auto client_name = make_table_text(client.name, false, true);
    controls::Grid::SetColumn(client_name, 1);
    client_identity.Children().Append(client_name);
    if (!client.identification_code.empty())
    {
      controls::ColumnDefinition code_column;
      code_column.Width(xaml::GridLengthHelper::Auto());
      client_identity.ColumnDefinitions().Append(code_column);
      auto code = make_table_text(client.identification_code);
      code.Name(L"ClientIdentificationCode");
      xaml::Automation::AutomationProperties::SetName(
          code, L"Identification code " + wide(client.identification_code));
      controls::Grid::SetColumn(code, 2);
      client_identity.Children().Append(code);
    }
    controls::Grid::SetColumn(client_identity, 1);
    row.Children().Append(client_identity);

    std::array values{make_table_text(client.backend),
                      make_table_text(client.role),
                      make_table_text(client.last_seen, true)};
    values[0].TextWrapping(xaml::TextWrapping::Wrap);
    values[0].TextTrimming(xaml::TextTrimming::None);
    for (std::size_t column = 0; column < values.size(); ++column)
    {
      controls::Grid::SetColumn(values[column], static_cast<int>(column + 2));
      row.Children().Append(values[column]);
    }

    const auto actions = controls::StackPanel();
    actions.Orientation(controls::Orientation::Horizontal);
    actions.Spacing(6);
    auto role_button = client.can_set_viewer
                           ? action_button(device_glyph(client.device_type), L"Demote to viewer")
                           : operator_action_button(client.device_type);
    role_button.IsEnabled(client.can_set_operator || client.can_set_viewer);
    role_button.Click(
        [context, id = client.id, demote = client.can_set_viewer](const auto&, const auto&)
        {
          if (demote)
            context.controller.set_viewer(id);
          else
            context.controller.set_operator(id);
          context.redraw();
        });
    auto identify_button = action_button(L'\xE81D', L"Identify client");
    identify_button.Name(L"IdentifyClientButton");
    // Inline dialog dismissal finishes after its controls disappear from the automation tree.
    identify_button.IsEnabled(client.connected && !state.identification);
    identify_button.Click(
        [context, id = client.id](const auto&, const auto&)
        {
          context.controller.identify_client(id);
          context.redraw();
        });
    auto forget_button = action_button(L'\xE74D', L"Forget client");
    forget_button.Click(
        [context, client](const auto&, const auto&)
        {
          context.confirm(L"Forget paired client",
                          L"Forget '" + wide(client.name) +
                              L"'? Its pairing credential will be deleted and the device will "
                              L"disconnect immediately.",
                          [context, id = client.id]
                          {
                            context.controller.forget_client(id);
                            context.redraw();
                          });
        });
    actions.Children().Append(identify_button);
    actions.Children().Append(role_button);
    auto teleprompter_button = teleprompter_action(client);
    teleprompter_button.Name(L"TeleprompterButton");
    teleprompter_button.Click(
        [context, id = client.id, name = client.name](const auto&, const auto&)
        {
          context.controller.teleprompter().open(id, name);
          context.redraw();
        });
    actions.Children().Append(teleprompter_button);
    if (client.production_provider == "obs" && client.remote_tally_allowed.has_value())
      actions.Children().Append(remote_tally_action(context, client));
    actions.Children().Append(forget_button);
    controls::Grid::SetColumn(actions, 5);
    actions.VerticalAlignment(xaml::VerticalAlignment::Center);
    row.Children().Append(actions);
    rows.Children().Append(row);
  }
  if (state.clients.empty())
  {
    auto empty = make_table_text("No paired clients yet. Pair a device to begin.", true);
    empty.Margin(xaml::Thickness{14, 18, 14, 18});
    rows.Children().Append(empty);
  }

  auto identify_all = find_control<controls::Button>(page, L"IdentifyAllButton");
  identify_all.IsEnabled(!state.identification &&
                         std::ranges::any_of(state.clients, &ManagementClientRow::connected));
  identify_all.Click(
      [context](const auto&, const auto&)
      {
        context.controller.identify_all_clients();
        context.redraw();
      });
  find_control<controls::Button>(page, L"RefreshButton")
      .Click(
          [context](const auto&, const auto&)
          {
            context.controller.refresh_clients();
            context.redraw();
          });
  find_control<controls::Button>(page, L"PairButton")
      .Click(
          [context](const auto&, const auto&)
          {
            context.controller.pair_client();
            context.redraw();
          });
  return page;
}
} // namespace studio_remote::windows
