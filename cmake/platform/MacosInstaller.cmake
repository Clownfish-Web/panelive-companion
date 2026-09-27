# The integration executable owns only privileged OBS placement, like the Windows Inno component.
add_library(studio_remote_macos_plugin_update STATIC
  packaging/macos/installer/integrations/plugin_update.cpp)
target_compile_features(studio_remote_macos_plugin_update PUBLIC cxx_std_20)
studio_remote_set_warnings(studio_remote_macos_plugin_update)
add_executable(panelive-companion-integrations
  packaging/macos/installer/integrations/integration_main.mm
  packaging/macos/installer/integrations/native_operations.mm)
set_target_properties(panelive-companion-integrations PROPERTIES
  OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
target_compile_options(panelive-companion-integrations PRIVATE -fobjc-arc)
target_link_libraries(panelive-companion-integrations PRIVATE
  studio_remote_macos_plugin_update "-framework Foundation")
studio_remote_set_warnings(panelive-companion-integrations)
