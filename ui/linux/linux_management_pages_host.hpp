#pragma once

#include "linux_management_pages.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace studio_remote::linux
{
// GTK-thread owner of editable page trees. Drafts live only in their native controls until
// window destruction. The scrolled window and controller are borrowed from the window owner.
class LinuxManagementPagesHost
{
public:
  LinuxManagementPagesHost(GtkScrolledWindow* body, std::string version);
  void show(ManagementPage page, LinuxPageContext context);

private:
  struct WidgetDeleter
  {
    void operator()(GtkWidget* widget) const noexcept;
  };
  using Page = std::unique_ptr<GtkWidget, WidgetDeleter>;
  Page create(ManagementPage page, LinuxPageContext context) const;

  GtkScrolledWindow* body_;
  std::string version_;
  std::map<ManagementPage, Page> forms_;
  Page teleprompter_form_;
  std::uint64_t teleprompter_form_generation_ = 0;
  std::optional<teleprompter::SessionAssignment> teleprompter_sharing_selection_;
};
} // namespace studio_remote::linux
