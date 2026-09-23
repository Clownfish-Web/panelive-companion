#include "winui_management_window.hpp"

#include "winui_management_pages.hpp"

#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

void WinUiManagementWindow::request_role_change(ManagementRoleChangeRequest request,
                                                ManagementApprovalDecision decision)
{
  const auto message = L"Allow '" + wide(request.device_name) + L"' to become " +
                       wide(request.requested_role) + L" for " + wide(request.backend_name) + L"?";
  show_approval(role_dialogs_,
                std::move(request.device_id),
                std::move(request.request_id),
                L"Client role request",
                message,
                std::move(decision));
}

void WinUiManagementWindow::request_global_setting_change(
    ManagementGlobalSettingChangeRequest request, ManagementApprovalDecision decision)
{
  const auto message = L"'" + wide(request.device_name) + L"' requested " +
                       wide(request.setting_name) + L".\n\n" + wide(request.warning);
  show_approval(global_dialogs_,
                std::move(request.device_id),
                std::move(request.request_id),
                L"Global setting request",
                message,
                std::move(decision));
}

void WinUiManagementWindow::show_approval(const std::shared_ptr<ApprovalDialogs>& dialogs,
                                          std::string device_id,
                                          std::string request_id,
                                          std::wstring title,
                                          std::wstring message,
                                          ManagementApprovalDecision decision)
{
  const ApprovalKey key{std::move(device_id), std::move(request_id)};
  activate();
  const auto dialog = make_management_dialog();
  dialog.Title(winrt::box_value(winrt::hstring(title)));
  dialog.Content(winrt::box_value(message));
  dialog.PrimaryButtonText(L"Approve");
  dialog.CloseButtonText(L"Deny");
  dialog.DefaultButton(controls::ContentDialogButton::Close);
  auto [entry, inserted] = dialogs->try_emplace(key);
  if (!inserted)
    throw std::logic_error("Native approval is already pending");
  entry->second.dialog = dialog;
  entry->second.decision = std::move(decision);
  // First activation returns before XAML attaches the visual tree. Keep the request cancellable
  // until Loaded supplies its root; the callback cannot retain the window or the dialog registry.
  if (const auto root = shell_.XamlRoot())
    present_approval(dialogs, key, root);
  else
    entry->second.loaded = shell_.Loaded(
        winrt::auto_revoke,
        [weak = std::weak_ptr<ApprovalDialogs>(dialogs), key](const auto& sender, const auto&)
        {
          if (const auto retained = weak.lock())
            present_approval(
                retained, key, sender.template as<xaml::FrameworkElement>().XamlRoot());
        });
}

void WinUiManagementWindow::present_approval(const std::shared_ptr<ApprovalDialogs>& dialogs,
                                             const ApprovalKey& key,
                                             const xaml::XamlRoot& root)
{
  const auto entry = dialogs->find(key);
  if (entry == dialogs->end())
    return;
  entry->second.loaded.revoke();
  try
  {
    const auto dialog = entry->second.dialog;
    dialog.XamlRoot(root);
    const auto operation = dialog.ShowAsync();
    operation.Completed(
        [weak = std::weak_ptr<ApprovalDialogs>(dialogs), key, identity = winrt::make_weak(dialog)](
            const auto& result, auto status)
        {
          bool approved = false;
          try
          {
            approved = status == winrt::Windows::Foundation::AsyncStatus::Completed &&
                       result.GetResults() == controls::ContentDialogResult::Primary;
          }
          catch (...)
          {
            OutputDebugStringW(L"Could not read the native approval result.\n");
          }
          if (const auto retained = weak.lock())
          {
            const auto current = retained->find(key);
            if (current != retained->end() && current->second.dialog == identity.get())
              finish_approval(*retained, key, approved);
          }
        });
  }
  catch (...)
  {
    OutputDebugStringW(L"Could not present the native approval dialog.\n");
    finish_approval(*dialogs, key, false);
  }
}

void WinUiManagementWindow::finish_approval(ApprovalDialogs& dialogs,
                                            const ApprovalKey& key,
                                            bool approved)
{
  const auto entry = dialogs.find(key);
  if (entry == dialogs.end())
    return;
  auto decision = std::move(entry->second.decision);
  dialogs.erase(entry);
  try
  {
    decision(approved);
  }
  catch (...)
  {
    OutputDebugStringW(L"Could not deliver the native approval result.\n");
  }
}

void WinUiManagementWindow::cancel_approval(ApprovalDialogs& dialogs,
                                            std::string_view device_id,
                                            std::string_view request_id)
{
  const ApprovalKey key{std::string(device_id), std::string(request_id)};
  const auto found = dialogs.find(key);
  if (found == dialogs.end())
    return;
  const auto dialog = found->second.dialog;
  finish_approval(dialogs, key, false);
  dialog.Hide();
}

void WinUiManagementWindow::cancel_role_change(std::string_view device_id,
                                               std::string_view request_id)
{
  cancel_approval(*role_dialogs_, device_id, request_id);
}

void WinUiManagementWindow::cancel_global_setting_change(std::string_view device_id,
                                                         std::string_view request_id)
{
  cancel_approval(*global_dialogs_, device_id, request_id);
}

} // namespace studio_remote::windows
