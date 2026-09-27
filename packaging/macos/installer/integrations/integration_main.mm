#include "native_operations.hpp"
#include "plugin_update.hpp"

#import <Foundation/Foundation.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

int main(int argc, char** argv)
{
  using namespace studio_remote::macos::installer;
  @autoreleasepool
  {
    try
    {
      const bool elevated = argc == 2 && std::string_view(argv[1]) == "--elevated";
      if (argc != 2 || (!elevated && std::string_view(argv[1]) != "--install"))
        throw std::invalid_argument("Run this component from the PaneLive Companion installer");
      if (elevated != (::geteuid() == 0))
        throw std::runtime_error("Run main installation as your normal user account");
      NSBundle* bundle = NSBundle.mainBundle;
      if (!bundle.bundlePath.fileSystemRepresentation ||
          !bundle.executablePath.fileSystemRepresentation)
        throw std::runtime_error("The installed integration bundle is required");
      const auto root = std::filesystem::path(bundle.bundlePath.fileSystemRepresentation);
      const auto executable = std::filesystem::path(bundle.executablePath.fileSystemRepresentation);
      const auto source = root / "Contents/Resources/panelive-companion-obs-plugin.plugin";
      if (![bundle.bundleIdentifier isEqualToString:@"panelive-companion-integrations"])
        throw std::runtime_error("The signed OBS integration component is required");
      verify_bundle(root);
      run_integration(
          elevated,
          [&]
          {
            verify_bundle(source);
            validate_shared_plugin_directory();
            update_plugin(source,
                          "/Library/Application "
                          "Support/obs-studio/plugins/panelive-companion-obs-plugin.plugin",
                          verify_bundle,
                          require_obs_closed,
                          ensure_shared_plugin_directory);
          },
          [&] { authorize_integration(executable); });
      std::cout << "Shared OBS helper installation completed\n";
      return 0;
    }
    catch (const std::exception& error)
    {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
}
