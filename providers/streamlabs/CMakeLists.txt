if(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
  add_library(studio_remote_streamlabs_adapter STATIC
    streamlabs_adapter.cpp
    streamlabs_protocol.cpp
    streamlabs_rpc_transport.cpp
    streamlabs_direct_catalogs.cpp
    streamlabs_direct_controls.cpp
    streamlabs_state.cpp)
  target_include_directories(studio_remote_streamlabs_adapter PUBLIC ../../include)
  target_link_libraries(studio_remote_streamlabs_adapter PUBLIC studio_remote_core)
  studio_remote_set_warnings(studio_remote_streamlabs_adapter)
endif()
