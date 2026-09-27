#include "macos_installer_operations.hpp"

#include "../../packaging/macos/installer/macos_installation_record.hpp"
#include "../../packaging/macos/installer/macos_obs_installation.hpp"
#include "../../platform/macos/platform_services/macos_config.hpp"
#include "../../platform/macos/platform_services/macos_production_installations.hpp"
#include "../../platform/macos/platform_services/macos_single_instance.hpp"
#include "macos_command_line.hpp"

#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace studio_remote::macos
{
namespace
{
void initialize_settings(const std::filesystem::path& directory)
{
  if (::geteuid() == 0)
    throw std::runtime_error("Initialize Companion settings as the user who runs Companion");
  MacosSingleInstance instance;
  if (!instance.owns_instance())
    throw std::runtime_error("Close Companion before initializing installation settings");
  ConfigStore store(directory);
  store.initialize_installation();
}
} // namespace

int run_installer_operation(const CommandLine& command_line)
{
  if (command_line.installer_operation == InstallerOperation::initialize_settings)
  {
    initialize_settings(command_line.data_directory);
    return 0;
  }
  const auto obs = installer::detect_obs_directory(command_line.obs_installation);
  if (obs.empty())
    throw std::runtime_error("Install OBS Studio before installing the Companion OBS integration");
  if (command_line.installer_operation == InstallerOperation::detect_obs)
  {
    std::cout << obs.string() << '\n';
    return 0;
  }
  if (command_line.installer_operation == InstallerOperation::record_installation)
  {
    if (::geteuid() == 0)
      throw std::runtime_error("Record installation as the user who runs Companion");
    installer::write_installation_record({companion_installation_directory(), obs});
    return 0;
  }
  throw std::invalid_argument("No installer operation selected");
}
} // namespace studio_remote::macos
