#pragma once

#include <optional>
#include <string_view>

namespace studio_remote
{
enum class ManagementInterfaceMode
{
  basic,
  advanced
};

enum class ManagementPage
{
  clients,
  teleprompter,
  adapters,
  companion_settings,
  status,
  diagnostics,
  advanced_settings,
  about
};

enum class ManagementSettingsSection
{
  interface_selection,
  access_control,
  background_operation,
  audio_source_audition_bus,
  audio,
  production_software_locations
};

constexpr std::string_view management_interface_mode_name(ManagementInterfaceMode mode) noexcept
{
  switch (mode)
  {
  case ManagementInterfaceMode::basic:
    return "basic";
  case ManagementInterfaceMode::advanced:
    return "advanced";
  }
  return "basic";
}

constexpr std::optional<ManagementInterfaceMode>
parse_management_interface_mode(std::string_view value) noexcept
{
  if (value == "basic")
    return ManagementInterfaceMode::basic;
  if (value == "advanced")
    return ManagementInterfaceMode::advanced;
  return std::nullopt;
}

constexpr ManagementInterfaceMode
management_interface_mode_or_default(std::string_view value) noexcept
{
  return parse_management_interface_mode(value).value_or(ManagementInterfaceMode::basic);
}

constexpr bool management_page_visible(ManagementInterfaceMode mode, ManagementPage page) noexcept
{
  if (mode == ManagementInterfaceMode::advanced)
    return true;
  return page == ManagementPage::teleprompter || page == ManagementPage::clients ||
         page == ManagementPage::adapters || page == ManagementPage::status ||
         page == ManagementPage::advanced_settings || page == ManagementPage::about;
}

constexpr bool management_settings_section_visible(ManagementInterfaceMode mode,
                                                   ManagementSettingsSection section) noexcept
{
  if (mode == ManagementInterfaceMode::advanced)
    return true;
  return section == ManagementSettingsSection::interface_selection ||
         section == ManagementSettingsSection::access_control ||
         section == ManagementSettingsSection::background_operation ||
         section == ManagementSettingsSection::audio_source_audition_bus;
}

constexpr ManagementPage visible_management_page(ManagementInterfaceMode mode,
                                                 ManagementPage requested) noexcept
{
  return management_page_visible(mode, requested) ? requested : ManagementPage::clients;
}

constexpr ManagementPage initial_management_page(ManagementInterfaceMode mode,
                                                 bool production_connection_configured,
                                                 bool network_recovery_required) noexcept
{
  if (network_recovery_required)
    return mode == ManagementInterfaceMode::advanced ? ManagementPage::companion_settings
                                                     : ManagementPage::status;
  return production_connection_configured ? ManagementPage::clients : ManagementPage::adapters;
}
} // namespace studio_remote
