#include "winui_management_pages.hpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

namespace
{
std::string client_identity(WinUiPageContext context)
{
  const auto& teleprompter = context.controller.teleprompter().state();
  const auto& clients = context.controller.state().clients;
  const auto client = std::ranges::find(clients, teleprompter.device_id, &ManagementClientRow::id);
  if (client == clients.end())
    return teleprompter.client_name;
  auto identity = client->name + " | Adapter: " + client->backend;
  if (!client->identification_code.empty())
    identity += " | Identification code: " + client->identification_code;
  return identity;
}

std::string local_datetime(std::uint64_t unix_ms)
{
  const auto seconds = static_cast<std::time_t>(unix_ms / 1000);
  std::tm local{};
  if (localtime_s(&local, &seconds) != 0)
    return "Unavailable";
  std::ostringstream formatted;
  formatted << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
  return formatted.str();
}

controls::Button icon_button(wchar_t glyph, std::wstring_view label)
{
  controls::Button button;
  button.Width(36);
  button.Height(34);
  button.Padding(xaml::Thickness{});
  controls::FontIcon icon;
  icon.FontFamily(management_icon_font());
  const wchar_t text[]{glyph, L'\0'};
  icon.Glyph(text);
  button.Content(icon);
  controls::ToolTipService::SetToolTip(button, winrt::box_value(winrt::hstring(label)));
  xaml::Automation::AutomationProperties::SetName(button, winrt::hstring(label));
  return button;
}

void cancel_edit(WinUiPageContext context)
{
  const auto discard = [context]
  {
    context.controller.teleprompter().cancel_edit();
    context.redraw();
  };
  if (context.controller.teleprompter().has_unsaved_changes())
    context.confirm(
        L"Discard script changes", L"Discard the unsaved teleprompter script changes?", discard);
  else
    discard();
}

controls::TextBox name_field(WinUiPageContext context, const teleprompter::Draft& draft)
{
  controls::TextBox name;
  name.Name(L"TeleprompterScriptName");
  name.Header(winrt::box_value(L"Script name"));
  name.Text(wide(draft.name));
  name.MaxLength(160);
  name.TextChanged(
      [context](const auto& sender, const auto&)
      {
        context.controller.teleprompter().set_name(
            narrow(sender.template as<controls::TextBox>().Text()));
      });
  return name;
}

controls::TextBox content_field(WinUiPageContext context, const teleprompter::Draft& draft)
{
  controls::TextBox content;
  content.Name(L"TeleprompterScriptContent");
  content.Header(winrt::box_value(L"Script content"));
  content.AcceptsReturn(true);
  content.TextWrapping(xaml::TextWrapping::Wrap);
  content.MinHeight(300);
  content.MaxHeight(420);
  content.MaxLength(static_cast<int>(teleprompter::maximum_content_bytes));
  content.Text(wide(draft.content));
  content.TextChanged(
      [context](const auto& sender, const auto&)
      {
        context.controller.teleprompter().set_content(
            narrow(sender.template as<controls::TextBox>().Text()));
      });
  return content;
}

controls::TextBlock cue_help()
{
  auto help = make_table_text(
      "Put [PAUSE:5] on its own line to hold at the center for five seconds with a countdown "
      "(1-60 seconds). Use [cue:Introduction] for a visible chapter marker on the player's "
      "timeline. Bracketed instructions are not spoken.",
      true);
  help.TextWrapping(xaml::TextWrapping::Wrap);
  return help;
}

controls::NumberBox speed_field(WinUiPageContext context, const teleprompter::Draft& draft)
{
  controls::NumberBox speed;
  speed.Name(L"TeleprompterReadingSpeed");
  speed.Header(winrt::box_value(L"Reading speed (words per minute)"));
  speed.Minimum(40);
  speed.Maximum(400);
  speed.Value(draft.words_per_minute);
  speed.SpinButtonPlacementMode(controls::NumberBoxSpinButtonPlacementMode::Inline);
  speed.HorizontalAlignment(xaml::HorizontalAlignment::Left);
  speed.Width(280);
  speed.ValueChanged(
      [context](const auto&, const controls::NumberBoxValueChangedEventArgs& args)
      {
        const auto value = args.NewValue();
        context.controller.teleprompter().set_reading_speed(
            std::isfinite(value) && value >= 40 && value <= 400 && std::floor(value) == value
                ? static_cast<std::uint32_t>(value)
                : 0);
      });
  return speed;
}

controls::StackPanel editor_actions(WinUiPageContext context)
{
  controls::StackPanel actions;
  actions.Orientation(controls::Orientation::Horizontal);
  actions.Spacing(8);
  controls::Button save;
  save.Name(L"SaveTeleprompterScript");
  save.Content(winrt::box_value(L"Save"));
  save.Click(
      [context](const auto&, const auto&)
      {
        context.controller.teleprompter().save_script();
        context.redraw();
      });
  actions.Children().Append(save);
  controls::Button cancel;
  cancel.Name(L"CancelTeleprompterScript");
  cancel.Content(winrt::box_value(L"Cancel"));
  cancel.Click([context](const auto&, const auto&) { cancel_edit(context); });
  actions.Children().Append(cancel);
  return actions;
}

controls::StackPanel editor(WinUiPageContext context)
{
  const auto& draft = context.controller.teleprompter().state().editor->draft;
  controls::StackPanel form;
  form.Spacing(16);
  form.Children().Append(name_field(context, draft));
  form.Children().Append(content_field(context, draft));
  form.Children().Append(cue_help());
  form.Children().Append(speed_field(context, draft));
  form.Children().Append(editor_actions(context));
  return form;
}

controls::Border script_row(WinUiPageContext context, const teleprompter::Summary& script)
{
  const auto& state = context.controller.teleprompter().state();
  const bool library = state.view == TeleprompterView::library;
  const bool selected = std::ranges::find(state.scripts, script.id, &teleprompter::Summary::id) !=
                        state.scripts.end();
  controls::Border border;
  if (!library && selected)
  {
    border.BorderBrush(xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 87, 214, 141}));
    border.BorderThickness(xaml::Thickness{2, 0, 0, 0});
  }
  border.Padding(xaml::Thickness{16, 12, 16, 12});
  border.CornerRadius(xaml::CornerRadius{6});
  border.Background(xaml::Media::SolidColorBrush(!library && selected
                                                     ? winrt::Windows::UI::Color{255, 32, 47, 43}
                                                     : winrt::Windows::UI::Color{255, 27, 33, 42}));
  controls::Grid grid;
  grid.ColumnSpacing(16);
  controls::ColumnDefinition text_column;
  text_column.Width(xaml::GridLengthHelper::FromValueAndType(1, xaml::GridUnitType::Star));
  grid.ColumnDefinitions().Append(text_column);
  controls::ColumnDefinition actions_column;
  actions_column.Width(xaml::GridLengthHelper::Auto());
  grid.ColumnDefinitions().Append(actions_column);
  controls::StackPanel info;
  info.Spacing(5);
  if (!library && !selected)
    info.Opacity(0.72);
  if (!library)
    info.Children().Append(make_status_badge(selected ? "Attached" : "Not attached",
                                             selected ? "healthy" : "neutral"));
  auto name = make_table_text(script.name, false, true);
  name.TextWrapping(xaml::TextWrapping::Wrap);
  info.Children().Append(name);
  auto dates =
      make_table_text("Created " + local_datetime(script.created_at_unix_ms) + "    Modified " +
                          local_datetime(script.modified_at_unix_ms) + " (local time)",
                      true);
  dates.TextWrapping(xaml::TextWrapping::Wrap);
  info.Children().Append(dates);
  info.Children().Append(
      make_table_text(std::to_string(script.word_count) + " words | Approx. " +
                          std::to_string((script.estimated_duration_ms + 999) / 1000) + " seconds",
                      true));
  grid.Children().Append(info);
  controls::StackPanel actions;
  actions.Orientation(controls::Orientation::Horizontal);
  actions.Spacing(6);
  actions.VerticalAlignment(xaml::VerticalAlignment::Center);
  if (library)
  {
    auto edit = icon_button(L'\xE70F', L"Edit " + wide(script.name));
    edit.Name(L"EditTeleprompterScript");
    edit.Click(
        [context, id = script.id](const auto&, const auto&)
        {
          context.controller.teleprompter().edit_script(id);
          context.redraw();
        });
    actions.Children().Append(edit);
    auto remove = icon_button(L'\xE74D', L"Delete " + wide(script.name));
    remove.Name(L"DeleteTeleprompterScript");
    remove.Click(
        [context, script](const auto&, const auto&)
        {
          context.confirm(L"Delete teleprompter script",
                          L"Delete '" + wide(script.name) +
                              L"' from the library and all attached clients?",
                          [context, id = script.id, revision = script.revision]
                          {
                            context.controller.teleprompter().delete_script(id, revision);
                            context.controller.refresh();
                            context.redraw();
                          });
        });
    actions.Children().Append(remove);
  }
  else
  {
    controls::Button assignment;
    assignment.Name(selected ? L"DetachTeleprompterScript" : L"AttachTeleprompterScript");
    assignment.Content(winrt::box_value(selected ? L"Detach" : L"Attach"));
    assignment.Background(
        xaml::Media::SolidColorBrush(selected ? winrt::Windows::UI::Color{255, 180, 35, 48}
                                              : winrt::Windows::UI::Color{255, 32, 92, 170}));
    assignment.Foreground(
        xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{255, 255, 255, 255}));
    xaml::Automation::AutomationProperties::SetName(
        assignment, (selected ? L"Detach " : L"Attach ") + wide(script.name));
    assignment.Click(
        [context, id = script.id, selected](const auto&, const auto&)
        {
          if (selected)
            context.controller.teleprompter().detach_script(id);
          else
            context.controller.teleprompter().attach_script(id);
          context.controller.refresh();
          context.redraw();
        });
    actions.Children().Append(assignment);
  }
  controls::Grid::SetColumn(actions, 1);
  grid.Children().Append(actions);
  border.Child(grid);
  return border;
}
} // namespace

xaml::FrameworkElement make_teleprompter_page(WinUiPageContext context)
{
  const auto page = load_xaml(LR"xaml(
<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" RowSpacing="16">
  <Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="Auto"/><RowDefinition Height="*"/></Grid.RowDefinitions>
  <Grid ColumnSpacing="16">
    <Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="Auto"/></Grid.ColumnDefinitions>
    <StackPanel Spacing="6">
      <TextBlock x:Name="Title" Text="Teleprompter" FontSize="28" FontWeight="SemiBold"/>
      <TextBlock x:Name="ClientName" Opacity="0.68" TextWrapping="Wrap"/>
    </StackPanel>
    <Button x:Name="BackToClients" Grid.Column="1" Content="Back to clients" VerticalAlignment="Center"/>
  </Grid>
  <TextBlock x:Name="TeleprompterError" Grid.Row="1" Foreground="#FF8080" TextWrapping="Wrap"/>
  <StackPanel x:Name="TeleprompterBody" Grid.Row="2" Spacing="12"/>
</Grid>)xaml");
  const auto& state = context.controller.teleprompter().state();
  const bool library = state.view == TeleprompterView::library;
  find_control<controls::TextBlock>(page, L"ClientName")
      .Text(library ? L"Manage scripts here. Attach them to devices from Clients."
                    : wide(client_identity(context)));
  find_control<controls::Button>(page, L"BackToClients")
      .Visibility(library ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
  find_control<controls::Button>(page, L"BackToClients")
      .Click(
          [context](const auto&, const auto&)
          {
            const auto close = [context]
            {
              context.controller.teleprompter().close();
              context.redraw();
            };
            if (context.controller.teleprompter().has_unsaved_changes())
              context.confirm(L"Discard script changes",
                              L"Discard the unsaved teleprompter script changes?",
                              close);
            else
              close();
          });
  auto body = find_control<controls::StackPanel>(page, L"TeleprompterBody");
  if (state.sharing)
  {
    find_control<controls::TextBlock>(page, L"Title").Text(L"Set up players");
    body.Children().Append(make_teleprompter_sharing(context));
  }
  else if (state.editor)
  {
    find_control<controls::TextBlock>(page, L"Title")
        .Text(state.editor->script_id ? L"Edit teleprompter script" : L"Add teleprompter script");
    body.Children().Append(editor(context));
  }
  else
  {
    if (library)
    {
      controls::Button add;
      add.Name(L"AddTeleprompterScript");
      add.Content(winrt::box_value(L"Add teleprompter script"));
      add.Click(
          [context](const auto&, const auto&)
          {
            context.controller.teleprompter().add_script();
            context.redraw();
          });
      body.Children().Append(add);
    }
    else
    {
      body.Children().Append(make_table_text(
          "Attach library scripts to this client. Create and edit scripts in Teleprompter.", true));
      body.Children().Append(make_teleprompter_sharing_actions(context));
    }
    if (state.library.empty())
      body.Children().Append(
          make_table_text("No scripts yet. Add a script in Teleprompter.", true));
    for (const auto& script : state.library)
      body.Children().Append(script_row(context, script));
  }
  refresh_teleprompter_page(page, context);
  return page;
}

void refresh_teleprompter_page(const xaml::FrameworkElement& page, WinUiPageContext context)
{
  auto error = find_control<controls::TextBlock>(page, L"TeleprompterError");
  error.Text(wide(context.controller.teleprompter().state().error));
  error.Visibility(error.Text().empty() ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
}
} // namespace studio_remote::windows
