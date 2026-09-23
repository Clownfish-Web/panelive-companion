#include "winui_page_helpers.hpp"

#include <algorithm>

namespace studio_remote::windows
{
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

std::string selected_choice(const controls::ComboBox& field)
{
  const auto item = field.SelectedItem().try_as<controls::ComboBoxItem>();
  return item ? narrow(winrt::unbox_value<winrt::hstring>(item.Tag())) : std::string{};
}

void refresh_choices(const controls::ComboBox& field,
                     std::vector<std::pair<std::string, std::string>> choices,
                     std::string_view initial_selection)
{
  if (field.IsDropDownOpen())
    return;
  auto selected = selected_choice(field);
  if (selected.empty())
    selected = initial_selection;
  if (!selected.empty() &&
      std::none_of(choices.begin(),
                   choices.end(),
                   [&selected](const auto& choice) { return choice.first == selected; }))
  {
    const auto item = field.SelectedItem().try_as<controls::ComboBoxItem>();
    const auto label = item ? narrow(winrt::unbox_value<winrt::hstring>(item.Content())) : selected;
    choices.emplace_back(selected, label);
  }
  bool unchanged = field.Items().Size() == choices.size();
  for (std::uint32_t index = 0; unchanged && index < field.Items().Size(); ++index)
  {
    const auto item = field.Items().GetAt(index).as<controls::ComboBoxItem>();
    unchanged = narrow(winrt::unbox_value<winrt::hstring>(item.Tag())) == choices[index].first &&
                narrow(winrt::unbox_value<winrt::hstring>(item.Content())) == choices[index].second;
  }
  if (unchanged)
    return;
  field.Items().Clear();
  for (const auto& [id, label] : choices)
  {
    const auto item = controls::ComboBoxItem();
    item.Tag(winrt::box_value(wide(id)));
    item.Content(winrt::box_value(wide(label)));
    field.Items().Append(item);
    if (id == selected)
      field.SelectedItem(item);
  }
}
} // namespace studio_remote::windows
