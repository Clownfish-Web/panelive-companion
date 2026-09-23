#include "winui_management_window.hpp"

#include <exception>
#include <utility>

namespace studio_remote::windows
{
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

void WinUiManagementWindow::cancel_client_identification()
{
  const auto pending = std::exchange(identification_dialog_, {});
  controller_.cancel_client_identification();
  if (pending)
  {
    pending->owner = nullptr;
    pending->dialog.Hide();
  }
}

void WinUiManagementWindow::finish_client_identification(
    const std::weak_ptr<IdentificationDialog>& weak,
    const IdentificationResult& result,
    const controls::CheckBox& audible,
    const controls::CheckBox& vibrate) noexcept
{
  const auto pending = weak.lock();
  auto* owner = pending ? std::exchange(pending->owner, nullptr) : nullptr;
  if (owner)
    owner->identification_dialog_.reset();
  try
  {
    try
    {
      // Observe asynchronous errors even after hide/shutdown invalidates the owner.
      const auto choice = result.GetResults();
      if (owner && choice == controls::ContentDialogResult::Primary)
        owner->controller_.confirm_client_identification(
            {audible.IsChecked().Value(), vibrate.IsChecked().Value()});
    }
    catch (const std::exception& error)
    {
      OutputDebugStringA(error.what());
      if (owner)
        owner->controller_.report_error(error.what());
    }
    if (owner)
    {
      owner->controller_.cancel_client_identification();
      owner->redraw_page();
    }
  }
  catch (...)
  {
    if (owner)
      owner->controller_.cancel_client_identification();
    OutputDebugStringW(L"Client identification dialog completion failed.\n");
  }
}

void WinUiManagementWindow::show_client_identification()
{
  const auto& request = controller_.state().identification;
  if (!request || identification_dialog_ || !shell_.XamlRoot())
    return;
  try
  {
    const auto content = controls::StackPanel();
    content.Spacing(12);
    const auto visual = controls::CheckBox();
    visual.Content(winrt::box_value(L"Visual (required)"));
    visual.IsChecked(true);
    visual.IsEnabled(false);
    const auto audible = controls::CheckBox();
    audible.Content(winrt::box_value(L"Audible"));
    audible.IsChecked(ClientIdentificationOptions{}.audible);
    const auto vibrate = controls::CheckBox();
    vibrate.Content(winrt::box_value(L"Vibrate"));
    vibrate.IsChecked(ClientIdentificationOptions{}.vibrate);
    for (const auto& choice : {visual, audible, vibrate})
      content.Children().Append(choice);
    const auto dialog = make_management_dialog();
    dialog.XamlRoot(shell_.XamlRoot());
    dialog.Title(winrt::box_value(winrt::hstring(wide(request->title))));
    dialog.Content(content);
    dialog.PrimaryButtonText(L"Identify");
    dialog.CloseButtonText(L"Cancel");
    dialog.DefaultButton(controls::ContentDialogButton::Close);
    identification_dialog_ = std::make_shared<IdentificationDialog>(dialog, this);
    const auto operation = dialog.ShowAsync();
    operation.Completed([weak = std::weak_ptr<IdentificationDialog>(identification_dialog_),
                         audible,
                         vibrate](const auto& result, auto) noexcept
                        { finish_client_identification(weak, result, audible, vibrate); });
  }
  catch (const std::exception& error)
  {
    cancel_client_identification();
    controller_.report_error(error.what());
    redraw_page();
  }
}
} // namespace studio_remote::windows
