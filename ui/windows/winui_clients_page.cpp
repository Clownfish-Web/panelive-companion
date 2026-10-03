#include "winui_management_pages.hpp"
#include "winui_pairing_preview.hpp"

#include "../shared/pairing_preview.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
// The host detaches the header from the page namescope; retain its command as presentation state.
struct ClientsPageState
    : winrt::implements<ClientsPageState, winrt::Windows::Foundation::IInspectable>
{
  explicit ClientsPageState(controls::Button button) : identify_all(std::move(button))
  {
  }
  controls::Button identify_all;
  bool refreshing_roster = false;
  std::optional<ManagementClientRow> inspector_client;
  controls::TextBlock inspector_connection{nullptr};
  controls::TextBlock inspector_last_seen{nullptr};
  controls::Button inspector_identify{nullptr};
};

ClientsPageState& client_page_state(const xaml::FrameworkElement& page)
{
  return *winrt::get_self<ClientsPageState>(
      page.Resources().Lookup(winrt::box_value(L"ClientsPageState")));
}

void update_text(const controls::TextBlock& label, std::wstring_view text)
{
  const winrt::hstring value(text);
  if (label.Text() != value)
    label.Text(value);
}

// Connection/time/identification updates do not change action callbacks or inspector structure.
auto inspector_content(const ManagementClientRow& client)
{
  return std::tie(client.id, client.name, client.backend, client.role, client.device_type,
                  client.can_set_operator, client.can_set_viewer, client.attached_script_count,
                  client.production_provider, client.remote_tally_allowed);
}

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
  <StackPanel Grid.Row="2" Spacing="20">
    <Grid x:Name="ClientToolbar" ColumnSpacing="12" RowSpacing="12">
      <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
      <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
      <Grid ColumnSpacing="8">
        <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
        <TextBox x:Name="ClientSearch" PlaceholderText="Name, backend or role" AutomationProperties.Name="Find client"/>
        <Button x:Name="ClearSearch" Grid.Column="1" Content="Clear" AutomationProperties.Name="Clear client search"/>
        <Button x:Name="ClientSort" Grid.Column="2" ToolTipService.ToolTip="Choose a column again to reverse its sort order"/>
      </Grid>
      <TextBlock x:Name="ClientCount" Grid.Column="1" VerticalAlignment="Center" Opacity="0.68"/>
    </Grid>
    <Grid x:Name="ClientColumns" ColumnSpacing="24" RowSpacing="24">
      <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="300"/></Grid.ColumnDefinitions>
      <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
      <StackPanel Spacing="0">
        <Grid Padding="12,0,12,12" BorderBrush="#343A44" BorderThickness="0,0,0,1">
          <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="72"/></Grid.ColumnDefinitions>
          <TextBlock Text="CLIENT" FontSize="12" FontWeight="SemiBold" Opacity="0.68"/>
          <TextBlock Grid.Column="1" Text="STATUS" FontSize="12" FontWeight="SemiBold" Opacity="0.68"/>
        </Grid>
        <ListView x:Name="ClientRows" SelectionMode="Single" HorizontalContentAlignment="Stretch"
                  ScrollViewer.VerticalScrollBarVisibility="Disabled" AutomationProperties.Name="Clients">
          <ListView.ItemContainerStyle>
            <Style TargetType="ListViewItem">
              <Setter Property="HorizontalContentAlignment" Value="Stretch"/>
              <Setter Property="Padding" Value="0"/>
              <Setter Property="Margin" Value="0"/>
            </Style>
          </ListView.ItemContainerStyle>
        </ListView>
        <TextBlock x:Name="EmptyClients" Margin="12,20" Opacity="0.68" TextWrapping="Wrap"/>
      </StackPanel>
      <Border x:Name="ClientInspector" Grid.Column="1" BorderBrush="#343A44" BorderThickness="1,0,0,0"
              Padding="24,0,0,0" VerticalAlignment="Top"/>
    </Grid>
  </StackPanel>
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
        context.confirm(title, detail,
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
// Keep icon tooltips and automation names while making inspector actions readable.
void label_action(const controls::Button& button, std::wstring_view label)
{
  auto icon = button.Content().as<xaml::UIElement>();
  button.Content(nullptr);
  controls::Grid content;
  content.ColumnSpacing(10);
  controls::ColumnDefinition icon_column;
  icon_column.Width(xaml::GridLengthHelper::Auto());
  content.ColumnDefinitions().Append(icon_column);
  controls::ColumnDefinition label_column;
  label_column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
  content.ColumnDefinitions().Append(label_column);
  content.Children().Append(icon);
  auto text = controls::TextBlock();
  text.Text(winrt::hstring(label));
  text.TextWrapping(xaml::TextWrapping::Wrap);
  controls::Grid::SetColumn(text, 1);
  content.Children().Append(text);
  button.Width(std::numeric_limits<double>::quiet_NaN());
  button.Height(std::numeric_limits<double>::quiet_NaN());
  button.MinHeight(36);
  button.Padding(xaml::Thickness{12, 8, 12, 8});
  button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
  button.HorizontalContentAlignment(xaml::HorizontalAlignment::Left);
  button.Content(content);
}

controls::TextBlock detail_text(std::string_view text, bool muted = false, bool semibold = false)
{
  auto result = make_table_text(text, muted, semibold);
  result.TextWrapping(xaml::TextWrapping::Wrap);
  result.TextTrimming(xaml::TextTrimming::None);
  return result;
}

void refresh_inspector(const xaml::FrameworkElement& page, WinUiPageContext context)
{
  auto& presentation = client_page_state(page);
  const auto& clients = context.controller.state().clients;
  const auto selected_id = narrow(winrt::unbox_value_or<winrt::hstring>(page.Tag(), L""));
  auto selected = std::ranges::find(clients, selected_id, &ManagementClientRow::id);
  auto inspector = find_control<controls::Border>(page, L"ClientInspector");
  inspector.Visibility(clients.empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  if (clients.empty())
  {
    page.Tag(nullptr);
    inspector.Child(nullptr);
    presentation.inspector_client.reset();
    presentation.inspector_connection = nullptr;
    presentation.inspector_last_seen = nullptr;
    presentation.inspector_identify = nullptr;
    return;
  }
  if (selected == clients.end())
    selected = clients.begin();
  const auto client = *selected;
  if (selected_id != client.id)
    page.Tag(winrt::box_value(wide(client.id)));
  if (presentation.inspector_client &&
      inspector_content(*presentation.inspector_client) == inspector_content(client))
  {
    update_text(presentation.inspector_connection, client.connected ? L"Online" : L"Offline");
    update_text(presentation.inspector_last_seen, wide(client.last_seen));
    presentation.inspector_identify.IsEnabled(client.connected &&
                                              !context.controller.state().identification);
    return;
  }
  controls::StackPanel body;
  body.Spacing(16);
  auto overline = detail_text("SELECTED CLIENT / " + client.id.substr(0, 8), true);
  overline.FontSize(12);
  overline.FontFamily(xaml::Media::FontFamily(L"Consolas"));
  controls::ToolTipService::SetToolTip(overline, winrt::box_value(wide(client.id)));
  body.Children().Append(overline);
  auto name = detail_text(client.name, false, true);
  name.FontSize(24);
  body.Children().Append(name);
  body.Children().Append(detail_text(client.backend + " \xC2\xB7 " + client.role, true));
  auto facts = load_xaml(LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" ColumnSpacing="20" RowSpacing="12">
  <Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
  <TextBlock Text="Connection" Opacity="0.68"/>
  <TextBlock Grid.Row="1" Text="Last seen" Opacity="0.68"/>
  <TextBlock Grid.Row="2" Text="Attached scripts" Opacity="0.68"/>
</Grid>)xaml")
                   .as<controls::Grid>();
  std::array values{
      detail_text(client.connected ? "Online" : "Offline"), detail_text(client.last_seen),
      detail_text(client.attached_script_count ? std::to_string(*client.attached_script_count)
                                               : "Unavailable")};
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    controls::Grid::SetColumn(values[index], 1);
    controls::Grid::SetRow(values[index], static_cast<int>(index));
    facts.Children().Append(values[index]);
  }
  presentation.inspector_connection = values[0];
  presentation.inspector_last_seen = values[1];
  body.Children().Append(facts);
  controls::Border divider;
  divider.Height(1);
  divider.Background(management_separator_brush());
  body.Children().Append(divider);
  const auto actions = controls::StackPanel();
  actions.Spacing(10);
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
  presentation.inspector_identify = identify_button;
  // Inline dialog dismissal finishes after its controls disappear from the automation tree.
  identify_button.IsEnabled(client.connected && !context.controller.state().identification);
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
  label_action(identify_button, L"Identify");
  label_action(role_button, client.can_set_viewer ? L"Set viewer" : L"Set operator");
  label_action(forget_button, L"Forget");
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
  label_action(teleprompter_button, L"Teleprompter");
  actions.Children().Append(teleprompter_button);
  if (client.production_provider == "obs" && client.remote_tally_allowed.has_value())
  {
    auto tally = remote_tally_action(context, client);
    label_action(tally, client.remote_tally_allowed.value() ? L"Withdraw web tally permission"
                                                            : L"Allow web tallies");
    actions.Children().Append(tally);
  }
  actions.Children().Append(forget_button);

  body.Children().Append(actions);
  inspector.Child(body);
  presentation.inspector_client = client;
}

void refresh_client_record(const controls::ListViewItem& item, const ManagementClientRow& client,
                           std::size_t ordinal)
{
  auto row = item.Content().try_as<xaml::FrameworkElement>();
  if (!row)
  {
    row = load_xaml(LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Padding="12,16" ColumnSpacing="12"
      BorderBrush="#343A44" BorderThickness="0,0,0,1">
  <Grid.ColumnDefinitions><ColumnDefinition Width="24"/><ColumnDefinition Width="*"/><ColumnDefinition Width="72"/></Grid.ColumnDefinitions>
  <TextBlock x:Name="Ordinal" FontFamily="Consolas" FontSize="12" Opacity="0.68" Margin="0,4,0,0"/>
  <StackPanel x:Name="Identity" Grid.Column="1" Spacing="8">
    <Grid ColumnSpacing="8">
      <Grid.ColumnDefinitions><ColumnDefinition Width="Auto"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions>
      <FontIcon x:Name="DeviceIcon" FontFamily="Segoe MDL2 Assets" FontSize="16"/>
      <TextBlock x:Name="ClientName" Grid.Column="1" FontSize="18" FontWeight="SemiBold" TextWrapping="Wrap"/>
    </Grid>
    <TextBlock x:Name="ClientCode" FontSize="12" FontFamily="Consolas" Opacity="0.68" TextWrapping="Wrap"/>
    <TextBlock x:Name="ClientFacts" Opacity="0.68" TextWrapping="Wrap"/>
    <TextBlock x:Name="ClientIdentificationCode" FontFamily="Consolas" FontWeight="SemiBold" TextWrapping="Wrap" Visibility="Collapsed"/>
  </StackPanel>
  <TextBlock x:Name="Connection" Grid.Column="2" Margin="0,4,0,0"/>
</Grid>)xaml");
    item.Tag(winrt::box_value(wide(client.id)));
    item.Content(row);
    xaml::Automation::AutomationProperties::SetAutomationId(item, L"client-row-" + wide(client.id));
    controls::ToolTipService::SetToolTip(find_control<controls::TextBlock>(row, L"ClientCode"),
                                         winrt::box_value(wide(client.id)));
  }
  update_text(find_control<controls::TextBlock>(row, L"Ordinal"),
              wide((ordinal < 10 ? "0" : "") + std::to_string(ordinal)));
  const wchar_t glyph[]{device_glyph(client.device_type), L'\0'};
  auto device = find_control<controls::FontIcon>(row, L"DeviceIcon");
  if (device.Glyph() != glyph)
    device.Glyph(glyph);
  update_text(find_control<controls::TextBlock>(row, L"ClientName"), wide(client.name));
  const auto scripts = client.attached_script_count
                           ? std::to_string(*client.attached_script_count) + " scripts"
                           : "attachments unavailable";
  update_text(find_control<controls::TextBlock>(row, L"ClientCode"),
              wide(client.id.substr(0, 8) + " \xC2\xB7 " + scripts));
  update_text(find_control<controls::TextBlock>(row, L"ClientFacts"),
              wide(client.backend + " \xC2\xB7 " + client.role + " \xC2\xB7 " + client.last_seen));
  auto status = find_control<controls::TextBlock>(row, L"Connection");
  const winrt::hstring connection = client.connected ? L"Online" : L"Offline";
  if (status.Text() != connection)
  {
    status.Text(connection);
    if (client.connected)
      status.Foreground(xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 87, 214, 141}));
    else
      status.ClearValue(controls::TextBlock::ForegroundProperty());
    status.Opacity(client.connected ? 1.0 : 0.68);
  }
  auto identification = find_control<controls::TextBlock>(row, L"ClientIdentificationCode");
  if (identification.Text() != wide(client.identification_code))
  {
    identification.Text(wide(client.identification_code));
    identification.Visibility(client.identification_code.empty() ? xaml::Visibility::Collapsed
                                                                 : xaml::Visibility::Visible);
    xaml::Automation::AutomationProperties::SetName(
        identification, L"Identification code " + wide(client.identification_code));
  }
  const auto accessible_name =
      wide(client.name + (client.connected ? ", Online, " : ", Offline, ") + client.backend + ", " +
           client.role);
  if (xaml::Automation::AutomationProperties::GetName(item) != accessible_name)
    xaml::Automation::AutomationProperties::SetName(item, accessible_name);
}

void refresh_roster(const xaml::FrameworkElement& page, WinUiPageContext context)
{
  // The edit remains mounted during periodic snapshots, preserving focus, caret and IME input.
  const auto query = find_control<controls::TextBox>(page, L"ClientSearch").Text();
  find_control<controls::Button>(page, L"ClearSearch").IsEnabled(!query.empty());
  refresh_inspector(page, context);
  const auto selected = winrt::unbox_value_or<winrt::hstring>(page.Tag(), L"");
  const auto rows = find_control<controls::ListView>(page, L"ClientRows");
  // Collection edits can raise SelectionChanged. Reconcile selection only after the IDs settle.
  auto& presentation = client_page_state(page);
  struct RefreshGuard
  {
    bool& refreshing;
    explicit RefreshGuard(bool& value) : refreshing(value)
    {
      refreshing = true;
    }
    ~RefreshGuard()
    {
      refreshing = false;
    }
  } guard(presentation.refreshing_roster);
  std::uint32_t visible = 0;
  std::size_t online = 0;
  const auto& clients = context.controller.state().clients;
  controls::ListViewItem selection{nullptr};
  for (const auto& client : clients)
  {
    const auto searchable = wide(client.name + " " + client.backend + " " + client.role);
    if (!query.empty() &&
        FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE,
                        searchable.data(), static_cast<int>(searchable.size()), query.c_str(),
                        static_cast<int>(query.size()), nullptr, nullptr, nullptr, 0) < 0)
      continue;
    // Leave unchanged items attached so WinUI does not replay entrance/selection animations.
    auto position = visible;
    while (position < rows.Items().Size() &&
           winrt::unbox_value<winrt::hstring>(
               rows.Items().GetAt(position).as<controls::ListViewItem>().Tag()) != wide(client.id))
      ++position;
    controls::ListViewItem item{nullptr};
    if (position < rows.Items().Size())
    {
      item = rows.Items().GetAt(position).as<controls::ListViewItem>();
      if (position != visible)
      {
        rows.Items().RemoveAt(position);
        rows.Items().InsertAt(visible, item);
      }
    }
    else
    {
      item = controls::ListViewItem();
      refresh_client_record(item, client, visible + 1);
      rows.Items().InsertAt(visible, item);
    }
    refresh_client_record(item, client, ++visible);
    online += client.connected ? 1U : 0U;
    if (wide(client.id) == selected)
      selection = item;
  }
  while (rows.Items().Size() > visible)
    rows.Items().RemoveAtEnd();
  if (rows.SelectedItem() != selection)
    rows.SelectedItem(selection);
  auto empty = find_control<controls::TextBlock>(page, L"EmptyClients");
  empty.Text(clients.empty() ? L"No paired clients yet. Pair a device to begin."
                             : L"No clients match your search.");
  empty.Visibility(visible == 0 ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
  update_text(find_control<controls::TextBlock>(page, L"ClientCount"),
              wide(std::to_string(visible) + " of " + std::to_string(clients.size()) +
                   " clients / " + std::to_string(online) + " online"));
}

void arrange_clients(const xaml::FrameworkElement& page, double width)
{
  const bool wide_layout = width >= 820;
  auto columns = find_control<controls::Grid>(page, L"ClientColumns");
  columns.ColumnDefinitions().GetAt(1).Width(
      xaml::GridLengthHelper::FromPixels(wide_layout ? 300 : 0));
  auto inspector = find_control<controls::Border>(page, L"ClientInspector");
  controls::Grid::SetColumn(inspector, wide_layout ? 1 : 0);
  controls::Grid::SetRow(inspector, wide_layout ? 0 : 1);
  inspector.BorderThickness(wide_layout ? xaml::Thickness{1, 0, 0, 0}
                                        : xaml::Thickness{0, 1, 0, 0});
  inspector.Padding(wide_layout ? xaml::Thickness{24, 0, 0, 0} : xaml::Thickness{0, 24, 0, 0});
  auto count = find_control<controls::TextBlock>(page, L"ClientCount");
  controls::Grid::SetColumn(count, wide_layout ? 1 : 0);
  controls::Grid::SetRow(count, wide_layout ? 0 : 1);
}

constexpr std::array sort_titles{L"Status", L"Client", L"Backend", L"Role", L"Last seen"};
} // namespace

void refresh_clients_page(const xaml::FrameworkElement& page, WinUiPageContext context)
{
  const auto& state = context.controller.state();
  auto pairing_host = find_control<controls::StackPanel>(page, L"PairingHost");
  const auto pairing_payload = state.pairing ? wide(state.pairing->payload) : std::wstring{};
  if (winrt::unbox_value_or<winrt::hstring>(pairing_host.Tag(), L"") != pairing_payload)
  {
    pairing_host.Children().Clear();
    if (state.pairing)
      pairing_host.Children().Append(make_pairing_card(*state.pairing, context.enlarge_pairing));
    pairing_host.Tag(winrt::box_value(pairing_payload));
  }
  client_page_state(page).identify_all.IsEnabled(
      !state.identification && std::ranges::any_of(state.clients, &ManagementClientRow::connected));
  const auto sort = state.client_sort;
  auto sort_button = find_control<controls::Button>(page, L"ClientSort");
  const auto sort_label =
      std::wstring(L"Sort: ") + sort_titles.at(static_cast<std::size_t>(sort.column)) +
      (sort.direction == ManagementSortDirection::ascending ? L" \u2191" : L" \u2193");
  if (winrt::unbox_value_or<winrt::hstring>(sort_button.Content(), L"") != sort_label)
    sort_button.Content(winrt::box_value(sort_label));
  refresh_roster(page, context);
}

xaml::FrameworkElement make_clients_page(WinUiPageContext context)
{
  if (!context.controller.teleprompter().state().device_id.empty())
    return make_teleprompter_page(context);
  const auto page = load_xaml(clients_markup);
  page.Resources().Insert(
      winrt::box_value(L"ClientsPageState"),
      winrt::make<ClientsPageState>(find_control<controls::Button>(page, L"IdentifyAllButton")));
  const auto weak_page = winrt::make_weak(page);
  page.SizeChanged(
      [weak_page](const auto&, const xaml::SizeChangedEventArgs& args)
      {
        if (const auto current = weak_page.get())
          arrange_clients(current, args.NewSize().Width);
      });
  find_control<controls::TextBox>(page, L"ClientSearch")
      .TextChanged(
          [weak_page, context](const auto&, const auto&)
          {
            if (const auto current = weak_page.get())
              refresh_roster(current, context);
          });
  find_control<controls::Button>(page, L"ClearSearch")
      .Click(
          [weak_page](const auto&, const auto&)
          {
            if (const auto current = weak_page.get())
            {
              auto search = find_control<controls::TextBox>(current, L"ClientSearch");
              search.Text(L"");
              search.Focus(xaml::FocusState::Programmatic);
            }
          });
  find_control<controls::ListView>(page, L"ClientRows")
      .SelectionChanged(
          [weak_page, context](const auto& sender, const auto&)
          {
            const auto item = sender.template as<controls::ListView>()
                                  .SelectedItem()
                                  .template try_as<controls::ListViewItem>();
            if (const auto current = weak_page.get();
                current && item && !client_page_state(current).refreshing_roster)
            {
              current.Tag(item.Tag());
              refresh_inspector(current, context);
            }
          });
  controls::MenuFlyout sort_menu;
  for (std::size_t index = 0; index < sort_titles.size(); ++index)
  {
    controls::MenuFlyoutItem choice;
    choice.Text(sort_titles[index]);
    choice.Click(
        [context, index](const auto&, const auto&)
        {
          context.controller.sort_clients(static_cast<ManagementClientColumn>(index));
          context.redraw();
        });
    sort_menu.Items().Append(choice);
  }
  find_control<controls::Button>(page, L"ClientSort").Flyout(sort_menu);
  find_control<controls::Button>(page, L"IdentifyAllButton")
      .Click(
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
  arrange_clients(page, 0);
  refresh_clients_page(page, context);
  return page;
}
} // namespace studio_remote::windows
