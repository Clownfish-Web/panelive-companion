#pragma once

#include <adwaita.h>
#include <studio_remote/ui/management_controller.hpp>

#include <memory>
#include <string>

namespace studio_remote::linux
{
class LinuxManagementWindow
{
public:
  LinuxManagementWindow(AdwApplication* application,
                        std::string companion_name,
                        std::string version,
                        ManagementUiController& controller);

  void activate();
  void hide();
  void close();
  void redraw();
  void request_role_change(ManagementRoleChangeRequest request,
                           ManagementApprovalDecision decision);
  void request_global_setting_change(ManagementGlobalSettingChangeRequest request,
                                     ManagementApprovalDecision decision);
  [[nodiscard]] GtkWindow* window() const noexcept;
  [[nodiscard]] ManagementPage current_page() const noexcept;

private:
  void select_page(ManagementPage page);
  void rebuild_navigation();
  GtkWidget* make_current_page();
  void show_client_identification();
  void cancel_client_identification();
  void finish_client_identification(int response) noexcept;
  struct IdentificationWindowDeleter
  {
    void operator()(GtkWindow* window) const noexcept;
  };

  std::string companion_name_;
  std::string version_;
  ManagementUiController& controller_;
  ManagementPage current_page_ = ManagementPage::clients;
  GtkWindow* window_ = nullptr;
  GtkWidget* navigation_ = nullptr;
  GtkWidget* content_ = nullptr;
  // Non-owning widgets retained by the GTK parent tree while the draft is unchanged.
  GtkWidget* teleprompter_form_ = nullptr;
  GtkWidget* error_bar_ = nullptr;
  std::uint64_t teleprompter_form_generation_ = 0;
  std::optional<teleprompter::SessionAssignment> teleprompter_sharing_selection_;
  std::unique_ptr<GtkWindow, IdentificationWindowDeleter> identification_dialog_;
  GtkCheckButton* identification_audible_ = nullptr;
  GtkCheckButton* identification_vibrate_ = nullptr;
};
} // namespace studio_remote::linux
