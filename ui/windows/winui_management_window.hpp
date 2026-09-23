#pragma once

#include "../shared/pairing_preview.hpp"
#include "../shared/pairing_qr.hpp"
#include "winui_management_pages_host.hpp"
#include <studio_remote/ui/management_controller.hpp>

#include <Windows.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace studio_remote::windows
{
// Invokes the composition callback only after WinUI has installed its control metadata and themes.
void run_winui_application(std::function<void()> launched);

class WinUiManagementWindow
{
public:
  WinUiManagementWindow(std::string gateway_name,
                        std::string build_version,
                        ManagementUiController& controller);

  void activate();
  void hide();
  void close();
  void refresh();
  void report_approval_error(std::string_view message);
  void show_pairing(ManagementPairing pairing);
  void clear_pairing();
  void show_confirmation(std::wstring title, std::wstring message, std::function<void()> confirmed);
  void request_role_change(ManagementRoleChangeRequest request,
                           ManagementApprovalDecision decision);
  void request_global_setting_change(ManagementGlobalSettingChangeRequest request,
                                     ManagementApprovalDecision decision);
  void cancel_role_change(std::string_view device_id, std::string_view request_id);
  void cancel_global_setting_change(std::string_view device_id, std::string_view request_id);

  [[nodiscard]] HWND window_handle() const;

private:
  using ApprovalKey = std::pair<std::string, std::string>;
  struct ApprovalDialog
  {
    winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog{nullptr};
    winrt::Microsoft::UI::Xaml::FrameworkElement::Loaded_revoker loaded;
    ManagementApprovalDecision decision;
  };
  using ApprovalDialogs = std::map<ApprovalKey, ApprovalDialog>;
  static void present_approval(const std::shared_ptr<ApprovalDialogs>& dialogs,
                               const ApprovalKey& key,
                               const winrt::Microsoft::UI::Xaml::XamlRoot& root);
  static void finish_approval(ApprovalDialogs& dialogs, const ApprovalKey& key, bool approved);
  void show_approval(const std::shared_ptr<ApprovalDialogs>& dialogs,
                     std::string device_id,
                     std::string request_id,
                     std::wstring title,
                     std::wstring message,
                     ManagementApprovalDecision decision);
  static void cancel_approval(ApprovalDialogs& dialogs,
                              std::string_view device_id,
                              std::string_view request_id);
  void rebuild_navigation();
  void select_page(ManagementPage page);
  void redraw_page();
  void open_pairing_preview();
  void update_pairing_preview();
  void close_pairing_preview();
  void show_client_identification();
  void cancel_client_identification();
  winrt::Microsoft::UI::Xaml::Controls::Button make_navigation_button(
      ManagementPage page, std::wstring_view label, wchar_t glyph, wchar_t secondary_glyph = L'\0');

  std::string gateway_name_;
  std::string build_version_;
  ManagementUiController& controller_;
  ManagementPage current_page_ = ManagementPage::clients;
  winrt::Microsoft::UI::Xaml::Window window_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid shell_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel primary_navigation_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::StackPanel footer_navigation_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ContentControl content_control_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid content_root_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid page_header_host_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer page_scroll_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid page_host_{nullptr};
  std::unique_ptr<WinUiManagementPagesHost> pages_;
  winrt::Microsoft::UI::Xaml::Controls::InfoBar error_bar_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::InfoBar recovery_bar_{nullptr};
  winrt::Microsoft::UI::Xaml::DispatcherTimer refresh_timer_{nullptr};
  winrt::Microsoft::UI::Xaml::DispatcherTimer pairing_preview_timer_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::Grid pairing_preview_host_{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBlock pairing_preview_countdown_{nullptr};
  PairingPreview pairing_preview_;
  PairingQrImage pairing_preview_image_;
  std::optional<ManagementPairing> pairing_preview_source_;
  bool shutting_down_ = false;
  // UI-thread state. Completion callbacks hold weak references and never retain this window.
  std::shared_ptr<ApprovalDialogs> role_dialogs_ = std::make_shared<ApprovalDialogs>();
  std::shared_ptr<ApprovalDialogs> global_dialogs_ = std::make_shared<ApprovalDialogs>();
  struct IdentificationDialog
  {
    winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog{nullptr};
    // UI-thread non-owning target. Weak completion never prolongs the window lifetime.
    WinUiManagementWindow* owner = nullptr;
  };
  using IdentificationResult = winrt::Windows::Foundation::IAsyncOperation<
      winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult>;
  static void finish_client_identification(
      const std::weak_ptr<IdentificationDialog>& weak,
      const IdentificationResult& result,
      const winrt::Microsoft::UI::Xaml::Controls::CheckBox& audible,
      const winrt::Microsoft::UI::Xaml::Controls::CheckBox& vibrate) noexcept;
  std::shared_ptr<IdentificationDialog> identification_dialog_;
};
} // namespace studio_remote::windows
