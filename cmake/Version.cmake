# The manifest pins the application version for both native build boundaries.
get_filename_component(studio_remote_manifest_path
  "${CMAKE_CURRENT_LIST_DIR}/../vcpkg.json" ABSOLUTE)
file(READ "${studio_remote_manifest_path}" studio_remote_manifest)
string(JSON STUDIO_REMOTE_BUILD_VERSION GET "${studio_remote_manifest}" version-semver)
if(NOT STUDIO_REMOTE_BUILD_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-([0-9A-Za-z.-]+))?$")
  message(FATAL_ERROR "vcpkg.json must declare a numeric version with an optional prerelease suffix")
endif()
set(STUDIO_REMOTE_VERSION "${CMAKE_MATCH_1}")
set(STUDIO_REMOTE_VERSION_PRERELEASE "${CMAKE_MATCH_3}")
set(STUDIO_REMOTE_PRODUCT_NAME "PaneLive Companion")
set(STUDIO_REMOTE_IS_PRERELEASE 0)
set(STUDIO_REMOTE_DEBIAN_VERSION "${STUDIO_REMOTE_VERSION}")
if(STUDIO_REMOTE_VERSION_PRERELEASE)
  set(STUDIO_REMOTE_IS_PRERELEASE 1)
  string(APPEND STUDIO_REMOTE_DEBIAN_VERSION "~${STUDIO_REMOTE_VERSION_PRERELEASE}")
endif()
if(NOT CMAKE_SCRIPT_MODE_FILE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${studio_remote_manifest_path}")
endif()

# Wire protocol versions are independent of the application release version.
set(STUDIO_REMOTE_PROTOCOL_VERSION "1.0.0")
set(STUDIO_REMOTE_PLUGIN_IPC_COMPATIBILITY ">=1.0.0 <2.0.0")
