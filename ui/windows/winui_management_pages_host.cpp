#include "winui_management_pages_host.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;

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

WinUiManagementPagesHost::WinUiManagementPagesHost(controls::Grid header_host,
                                                   controls::Grid body_host,
                                                   std::string build_version)
    : header_host_(std::move(header_host)), body_host_(std::move(body_host)),
      build_version_(std::move(build_version))
{
}

WinUiManagementPagesHost::PageView WinUiManagementPagesHost::create(ManagementPage kind,
                                                                    WinUiPageContext context) const
{
  xaml::FrameworkElement page{nullptr};
  switch (kind)
  {
  case ManagementPage::clients:
    page = make_clients_page(context);
    break;
  case ManagementPage::teleprompter:
    if (context.controller.teleprompter().state().view != TeleprompterView::library)
      context.controller.teleprompter().open_library();
    page = make_teleprompter_page(context);
    break;
  case ManagementPage::adapters:
    page = make_adapters_page(context);
    break;
  case ManagementPage::companion_settings:
    page = make_companion_settings_page(context);
    break;
  case ManagementPage::status:
    page = make_status_page(context);
    break;
  case ManagementPage::diagnostics:
    page = make_diagnostics_page(context);
    break;
  case ManagementPage::advanced_settings:
    page = make_advanced_settings_page(context);
    break;
  case ManagementPage::about:
    page = make_about_page(build_version_);
    break;
  }
  const auto layout = page.try_as<controls::Grid>();
  if (!layout || layout.Children().Size() == 0 || layout.RowDefinitions().Size() == 0)
    throw std::logic_error("WinUI management pages require a dedicated header row");
  const auto header = layout.Children().GetAt(0);
  layout.Children().RemoveAt(0);
  layout.RowDefinitions().RemoveAt(0);
  for (const auto& child : layout.Children())
  {
    const auto element = child.as<xaml::FrameworkElement>();
    controls::Grid::SetRow(element, std::max(0, controls::Grid::GetRow(element) - 1));
  }
  return {header, page};
}

void WinUiManagementPagesHost::show(ManagementPage kind, WinUiPageContext context)
{
  const bool editable =
      kind == ManagementPage::adapters || kind == ManagementPage::companion_settings ||
      kind == ManagementPage::advanced_settings || kind == ManagementPage::diagnostics;
  const bool clients = kind == ManagementPage::clients &&
                       context.controller.teleprompter().state().device_id.empty();
  PageView view;
  const auto& teleprompter = context.controller.teleprompter().state();
  if (!teleprompter.editor && !teleprompter.sharing)
    teleprompter_form_.reset();
  if ((kind == ManagementPage::clients || kind == ManagementPage::teleprompter) &&
      (teleprompter.editor || teleprompter.sharing))
  {
    // Sharing choices change dependent controls. Retain them only while the draft is unchanged;
    // periodic refresh must still leave an open picker or script editor attached.
    if (!teleprompter_form_ || teleprompter_form_generation_ != teleprompter.form_generation ||
        !same_sharing_selection(teleprompter_sharing_selection_, teleprompter.sharing))
    {
      teleprompter_form_ = create(kind, context);
      teleprompter_form_generation_ = teleprompter.form_generation;
      teleprompter_sharing_selection_ = teleprompter.sharing;
    }
    view = *teleprompter_form_;
    refresh_teleprompter_page(view.body, context);
  }
  else if (editable || clients)
  {
    auto found = forms_.find(kind);
    if (found == forms_.end())
      found = forms_.emplace(kind, create(kind, context)).first;
    view = found->second;
    switch (kind)
    {
    case ManagementPage::clients:
      refresh_clients_page(view.body, context);
      break;
    case ManagementPage::adapters:
      refresh_adapters_page(view.body, context);
      break;
    case ManagementPage::companion_settings:
      refresh_companion_settings_page(view.body, context);
      break;
    case ManagementPage::advanced_settings:
      refresh_advanced_settings_page(view.body, context);
      break;
    case ManagementPage::diagnostics:
      refresh_diagnostics_page(view.body, context);
      break;
    default:
      break;
    }
  }
  else
    view = create(kind, context);

  // Even detaching and reattaching the same controls interrupts focus, selection and IME input.
  if (body_host_.Children().Size() != 0 && body_host_.Children().GetAt(0) == view.body)
    return;
  header_host_.Children().Clear();
  body_host_.Children().Clear();
  header_host_.Children().Append(view.header);
  body_host_.Children().Append(view.body);
}
} // namespace studio_remote::windows
