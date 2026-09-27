#include "linux_management_pages_host.hpp"

#include <utility>

namespace studio_remote::linux
{
namespace
{
bool same_sharing_selection(const std::optional<teleprompter::SessionAssignment>& previous,
                            const std::optional<teleprompter::SessionAssignment>& current)
{
  if (!previous || !current)
    return previous.has_value() == current.has_value();
  return previous->script_ids == current->script_ids && previous->members == current->members &&
         previous->owner_role == current->owner_role;
}
} // namespace

LinuxManagementPagesHost::LinuxManagementPagesHost(GtkScrolledWindow* body, std::string version)
    : body_(body), version_(std::move(version))
{
}

void LinuxManagementPagesHost::WidgetDeleter::operator()(GtkWidget* widget) const noexcept
{
  g_object_unref(widget);
}

LinuxManagementPagesHost::Page LinuxManagementPagesHost::create(ManagementPage kind,
                                                                LinuxPageContext context) const
{
  GtkWidget* page = nullptr;
  switch (kind)
  {
  case ManagementPage::clients:
    page = make_linux_clients_page(context);
    break;
  case ManagementPage::teleprompter:
    if (context.controller.teleprompter().state().view != TeleprompterView::library)
      context.controller.teleprompter().open_library();
    page = make_linux_teleprompter_page(context);
    break;
  case ManagementPage::adapters:
    page = make_linux_adapters_page(context);
    break;
  case ManagementPage::companion_settings:
    page = make_linux_companion_settings_page(context);
    break;
  case ManagementPage::status:
    page = make_linux_status_page(context);
    break;
  case ManagementPage::diagnostics:
    page = make_linux_diagnostics_page(context);
    break;
  case ManagementPage::advanced_settings:
    page = make_linux_advanced_settings_page(context);
    break;
  case ManagementPage::about:
    page = make_linux_about_page(version_);
    break;
  }
  g_object_ref_sink(page);
  return Page(page);
}

void LinuxManagementPagesHost::show(ManagementPage kind, LinuxPageContext context)
{
  const bool editable =
      kind == ManagementPage::adapters || kind == ManagementPage::companion_settings ||
      kind == ManagementPage::advanced_settings || kind == ManagementPage::diagnostics;
  Page transient;
  GtkWidget* page = nullptr;
  const auto& teleprompter = context.controller.teleprompter().state();
  // Retain interaction only while the roster is selected, not inactive pairing material.
  if (kind != ManagementPage::clients || !teleprompter.device_id.empty())
    clients_page_.reset();
  if (!teleprompter.editor && !teleprompter.sharing)
    teleprompter_form_.reset();
  if ((kind == ManagementPage::clients || kind == ManagementPage::teleprompter) &&
      (teleprompter.editor || teleprompter.sharing))
  {
    if (!teleprompter_form_ || teleprompter_form_generation_ != teleprompter.form_generation ||
        !same_sharing_selection(teleprompter_sharing_selection_, teleprompter.sharing))
    {
      teleprompter_form_ = create(kind, context);
      teleprompter_form_generation_ = teleprompter.form_generation;
      teleprompter_sharing_selection_ = teleprompter.sharing;
    }
    page = teleprompter_form_.get();
    refresh_linux_teleprompter_page(page, context);
  }
  else if (kind == ManagementPage::clients && teleprompter.device_id.empty())
  {
    if (!clients_page_)
      clients_page_ = create(kind, context);
    else
      refresh_linux_clients_page(clients_page_.get(), context);
    page = clients_page_.get();
  }
  else if (editable)
  {
    auto found = forms_.find(kind);
    if (found == forms_.end())
      found = forms_.emplace(kind, create(kind, context)).first;
    page = found->second.get();
    switch (kind)
    {
    case ManagementPage::adapters:
      refresh_linux_adapters_page(page, context);
      break;
    case ManagementPage::companion_settings:
      refresh_linux_companion_settings_page(page, context);
      break;
    case ManagementPage::advanced_settings:
      refresh_linux_advanced_settings_page(page, context);
      break;
    case ManagementPage::diagnostics:
      refresh_linux_diagnostics_page(page, context);
      break;
    default:
      break;
    }
  }
  else
  {
    transient = create(kind, context);
    page = transient.get();
  }

  // GtkScrolledWindow inserts a viewport for non-scrollable children. Do not detach the
  // current page: even reattaching the same widget interrupts focus, selection and IME input.
  auto* child = gtk_scrolled_window_get_child(body_);
  if (GTK_IS_VIEWPORT(child))
    child = gtk_viewport_get_child(GTK_VIEWPORT(child));
  if (child != page)
    gtk_scrolled_window_set_child(body_, page);
}
} // namespace studio_remote::linux
