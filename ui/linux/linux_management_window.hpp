#pragma once

#include "../shared/pairing_preview.hpp"
#include "../shared/pairing_qr.hpp"
#include "linux_approval_dialogs.hpp"
#include "linux_management_pages_host.hpp"

#include <adwaita.h>
#include <studio_remote/ui/management_controller.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace studio_remote::linux
{
class LinuxManagementWindow
{
public:
  LinuxManagementWindow(AdwApplication* application,
                        std::string companion_name,
                        std::string version,
                        ManagementUiController& controller);
  ~LinuxManagementWindow();

  void activate();
  void hide();
  void close();
  void redraw();
  void clear_pairing();
  void request_role_change(ManagementRoleChangeRequest request,
                           ManagementApprovalDecision decision);
  void request_global_setting_change(ManagementGlobalSettingChangeRequest request,
                                     ManagementApprovalDecision decision);
  void cancel_role_change(std::string_view device_id, std::string_view request_id);
  void cancel_global_setting_change(std::string_view device_id, std::string_view request_id);
  [[nodiscard]] GtkWindow* window() const noexcept;
  [[nodiscard]] ManagementPage current_page() const noexcept;

private:
  void select_page(ManagementPage page);
  void rebuild_navigation();
  void create_navigation();
  void update_navigation_width(int width) noexcept;
  void apply_navigation_layout() noexcept;
  void observe_window_size() noexcept;
  void stop_observing_window_size() noexcept;
  void queue_layout_redraw() noexcept;
  void cancel_layout_redraw() noexcept;
  void create_content();
  void open_pairing_preview();
  void update_pairing_preview();
  void close_pairing_preview();
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
  std::string selected_client_id_;
  std::string client_query_;
  GtkWindow* window_ = nullptr;
  GtkWidget* navigation_ = nullptr;
  // Borrowed GTK-thread widgets owned by the window; retained across snapshot refreshes.
  GtkWidget* navigation_scroll_ = nullptr;
  GtkWidget* navigation_space_ = nullptr;
  GtkWidget* navigation_toggle_ = nullptr;
  std::map<ManagementPage, GtkWidget*> navigation_buttons_;
  std::vector<GtkWidget*> navigation_labels_;
  bool navigation_expanded_ = true;
  bool wide_navigation_expanded_ = true;
  bool narrow_window_ = false;
  GdkSurface* observed_surface_ = nullptr;
  gulong surface_layout_handler_ = 0;
  // GTK-thread-only, at most one pending presentation redraw; no backend polling.
  guint layout_redraw_source_ = 0;
  int observed_width_ = 0;
  GtkWidget* content_ = nullptr;
  GtkWidget* pairing_preview_host_ = nullptr;
  GtkWidget* pairing_preview_countdown_ = nullptr;
  PairingPreview pairing_preview_;
  PairingQrImage pairing_preview_image_;
  std::optional<ManagementPairing> pairing_preview_source_;
  guint pairing_preview_timer_ = 0;
  GtkWidget* error_bar_ = nullptr;
  GtkWidget* recovery_card_ = nullptr;
  GtkWidget* recovery_message_ = nullptr;
  GtkWidget* footer_status_ = nullptr;
  std::unique_ptr<LinuxManagementPagesHost> pages_;
  std::unique_ptr<GtkWindow, IdentificationWindowDeleter> identification_dialog_;
  GtkCheckButton* identification_audible_ = nullptr;
  GtkCheckButton* identification_vibrate_ = nullptr;
  LinuxApprovalDialogs role_dialogs_;
  LinuxApprovalDialogs global_dialogs_;
};
} // namespace studio_remote::linux
