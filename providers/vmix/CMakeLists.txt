if(STUDIO_REMOTE_ENABLE_PROVIDER_VMIX)
  add_library(studio_remote_vmix_adapter STATIC
    vmix_adapter.cpp
    vmix_master_mute.cpp
    vmix_output_geometry.cpp
    vmix_telestrator_discovery.cpp
    vmix_state.cpp)
  target_include_directories(studio_remote_vmix_adapter PUBLIC ../../include
    PRIVATE "${CMAKE_BINARY_DIR}/generated/build_config")
  target_link_libraries(studio_remote_vmix_adapter PUBLIC studio_remote_core)
  studio_remote_set_warnings(studio_remote_vmix_adapter)
endif()
