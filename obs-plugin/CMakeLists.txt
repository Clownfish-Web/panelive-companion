cmake_minimum_required(VERSION 3.28)
if(CMAKE_HOST_APPLE)
  include(../cmake/platform/MacosDeployment.cmake)
endif()
include(../cmake/Version.cmake)
project(panelive-companion-obs-plugin VERSION ${STUDIO_REMOTE_VERSION}
  DESCRIPTION "${STUDIO_REMOTE_PRODUCT_NAME}" LANGUAGES CXX)

include(GNUInstallDirs)

set(OBS_PLUGIN_PACKAGE_ID "panelive-companion-obs-plugin")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(OBS_SOURCE_DIR "" CACHE PATH "Path to a matching OBS Studio source tree")
set(OBS_IMPORT_LIBRARY_DIR "" CACHE PATH "Directory containing obs.lib and obs-frontend-api.lib")
set(OBS_QT_DIR "" CACHE PATH "Path to the matching OBS Qt development package")

if(MSVC AND MSVC_VERSION LESS 1930)
  message(FATAL_ERROR
    "OBS 32.2.1 uses Qt 6.11.1 and requires Visual Studio 2022/MSVC 19.30 or newer. "
    "This compiler is MSVC ${MSVC_VERSION}.")
endif()

if(WIN32)
  if(NOT EXISTS "${OBS_SOURCE_DIR}/libobs/obs-module.h")
    message(FATAL_ERROR "OBS_SOURCE_DIR does not contain the OBS headers")
  endif()
  if(NOT EXISTS "${OBS_IMPORT_LIBRARY_DIR}/obs.lib")
    message(FATAL_ERROR "OBS_IMPORT_LIBRARY_DIR does not contain obs.lib")
  endif()
  if(NOT EXISTS "${OBS_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "OBS_QT_DIR does not contain the matching Qt6 development package")
  endif()
  list(PREPEND CMAKE_PREFIX_PATH "${OBS_QT_DIR}")
  # CMake 4.4 can stall while Ninja probes Unix pthread libraries with MSVC. These libraries are
  # never valid for the Windows ABI; Qt uses the native Win32 thread backend on this platform.
  set(CMAKE_HAVE_LIBC_PTHREAD 0 CACHE INTERNAL "MSVC does not provide libc pthreads")
  set(CMAKE_HAVE_PTHREADS_CREATE 0 CACHE INTERNAL "MSVC does not provide pthreads.lib")
  set(CMAKE_HAVE_PTHREAD_CREATE 0 CACHE INTERNAL "MSVC does not provide pthread.lib")
  set(HAVE_STDATOMIC 1 CACHE INTERNAL "MSVC provides standard C++ atomics")
  find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Widgets)
elseif(APPLE)
  include(cmake/macos_dependencies.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(libobs CONFIG REQUIRED)
  if(libobs_VERSION VERSION_LESS 30.0)
    message(FATAL_ERROR "The Linux OBS helper release requires the OBS 30 shared-library ABI")
  endif()
  find_package(obs-frontend-api CONFIG REQUIRED)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)
else()
  message(FATAL_ERROR "The OBS helper plugin supports Windows, macOS, and Linux")
endif()

set(OBS_PLUGIN_COMMON_SOURCES
  src/capture_lifecycle.cpp
  src/capture_lifecycle.hpp
  src/companion_capture_status.hpp
  src/frontend_reload_barrier.cpp
  src/frontend_reload_barrier.hpp
  src/obs_frame_capture.cpp
  src/obs_frame_capture.hpp
  src/obs_bgra_converter.cpp
  src/obs_bgra_converter.hpp
  src/obs_audio_monitor.cpp
  src/obs_audio_monitor_mapping.cpp
  src/obs_audio_monitor.hpp
  src/obs_audio_mix_policy.cpp
  src/obs_audio_mix_policy.hpp
  src/obs_audio_mixer_roster.cpp
  src/obs_audio_mixer_roster.hpp
  src/obs_audio_source_ingress.cpp
  src/obs_audio_source_ingress.hpp
  src/obs_preview_output.cpp
  src/obs_preview_output.hpp
  src/obs_quick_transition_control.cpp
  src/obs_quick_transition_control.hpp
  src/obs_quick_transition_duration_ui.cpp
  src/obs_quick_transition_duration_ui.hpp
  src/obs_runtime_capabilities_publisher.cpp
  src/obs_runtime_capabilities_publisher.hpp
  src/obs_scene_item_properties.cpp
  src/obs_scene_item_properties.hpp
  src/obs_source_thumbnail_renderer.cpp
  src/obs_source_thumbnail_renderer.hpp
  src/recording_pause_policy.cpp
  src/recording_pause_policy.hpp)
list(APPEND OBS_PLUGIN_COMMON_SOURCES
  src/shared_thumbnail_mailbox.cpp
  src/shared_thumbnail_mailbox.hpp
  src/shared_thumbnail_publication.cpp
  src/shared_thumbnail_publication.hpp
  src/thumbnail_capture_policy.hpp
  src/thumbnail_frame_readiness.hpp)

set(OBS_PLUGIN_WINDOWS_SOURCES
  src/windows/owner_security.cpp
  src/windows/shared_memory_owner.cpp
  src/obs_frontend_symbols_windows.cpp
  src/obs_companion_status_ui.cpp
  src/obs_companion_status_ui.hpp
  src/plugin_main.cpp
  src/shared_frame_publisher.cpp
  src/shared_frame_publication.cpp
  src/shared_frame_publisher.hpp
  )

if(WIN32)
  add_library(panelive-companion-obs-plugin MODULE
    ${OBS_PLUGIN_COMMON_SOURCES}
    ${OBS_PLUGIN_WINDOWS_SOURCES}
    src/windows/panelive-companion-obs-plugin.rc)
  set_source_files_properties(src/windows/panelive-companion-obs-plugin.rc PROPERTIES
    COMPILE_DEFINITIONS
      "PROJECT_VERSION=\"${STUDIO_REMOTE_BUILD_VERSION}\";PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR};PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH};STUDIO_REMOTE_IS_PRERELEASE=${STUDIO_REMOTE_IS_PRERELEASE}")
elseif(APPLE)
  add_library(panelive-companion-obs-plugin MODULE
    ${OBS_PLUGIN_COMMON_SOURCES}
    src/plugin_main.cpp
    src/obs_companion_status_ui.cpp
    src/macos/obs_frontend_symbols_macos.cpp
    src/macos/shared_memory_owner.cpp
    src/macos/shared_frame_publisher_macos.cpp
    src/shared_frame_publication.cpp)
  set_target_properties(panelive-companion-obs-plugin PROPERTIES
    BUNDLE TRUE BUNDLE_EXTENSION plugin
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_plugin_Info.plist.in")
  target_sources(panelive-companion-obs-plugin PRIVATE LICENSE data/locale/en-US.ini)
  set_source_files_properties(LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties(data/locale/en-US.ini PROPERTIES MACOSX_PACKAGE_LOCATION Resources/locale)
  set_property(TARGET panelive-companion-obs-plugin APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/locale/en-US.ini"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_plugin_Info.plist.in")
  target_compile_options(panelive-companion-obs-plugin PRIVATE
    -Wall -Wextra -Werror -Werror=unguarded-availability-new)
  set(OBS_PLUGIN_CODESIGN_IDENTITY "-" CACHE STRING "Helper signing identity; - is local ad-hoc signing")
  add_custom_command(TARGET panelive-companion-obs-plugin POST_BUILD
    COMMAND /usr/bin/codesign --force --sign "${OBS_PLUGIN_CODESIGN_IDENTITY}"
      "$<TARGET_BUNDLE_DIR:panelive-companion-obs-plugin>"
    COMMAND /usr/bin/codesign --verify --strict
      "$<TARGET_BUNDLE_DIR:panelive-companion-obs-plugin>"
    VERBATIM)
else()
  add_library(panelive-companion-obs-plugin MODULE
    ${OBS_PLUGIN_COMMON_SOURCES}
    src/plugin_main.cpp
    src/obs_companion_status_ui.cpp
    src/obs_frontend_symbols_linux.cpp
    src/shared_frame_publisher.hpp
    src/shared_frame_publisher_linux.cpp)
endif()

target_include_directories(panelive-companion-obs-plugin PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/generated"
  "${OBS_SOURCE_DIR}/libobs"
  "${OBS_SOURCE_DIR}/frontend/api"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include")

if(WIN32)
  target_link_libraries(panelive-companion-obs-plugin PRIVATE
    "${OBS_IMPORT_LIBRARY_DIR}/obs.lib"
    "${OBS_IMPORT_LIBRARY_DIR}/obs-frontend-api.lib"
    Qt6::Core
    Qt6::Widgets
    advapi32
    avrt)
  target_compile_definitions(panelive-companion-obs-plugin PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE)
else()
  target_link_libraries(panelive-companion-obs-plugin PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets)
endif()
target_compile_definitions(panelive-companion-obs-plugin PRIVATE
  OBS_INSTALL_PREFIX=".")

set_target_properties(panelive-companion-obs-plugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "${OBS_PLUGIN_PACKAGE_ID}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(panelive-companion-obs-plugin PRIVATE ${CMAKE_DL_LIBS})
  target_link_options(panelive-companion-obs-plugin PRIVATE
    $<$<CONFIG:Release>:-Wl,--strip-all>)
endif()

if(WIN32)
  set(OBS_PLUGIN_BINARY_DESTINATION "obs-plugins/64bit")
  set(OBS_PLUGIN_DATA_DESTINATION "data/obs-plugins/${OBS_PLUGIN_PACKAGE_ID}")
elseif(APPLE)
  set(OBS_PLUGIN_BINARY_DESTINATION "obs-plugins")
  set(OBS_PLUGIN_DATA_DESTINATION "obs-plugins/${OBS_PLUGIN_PACKAGE_ID}.plugin/Contents/Resources")
elseif(STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "ARCH")
  if(NOT DEFINED OBS_PLUGIN_LINUX_INSTALL_DIR)
    if(NOT CMAKE_LIBRARY_ARCHITECTURE)
      message(FATAL_ERROR
        "Set OBS_PLUGIN_LINUX_INSTALL_DIR to the native OBS plugin directory on this distribution")
    endif()
    set(OBS_PLUGIN_LINUX_INSTALL_DIR
      "${CMAKE_INSTALL_LIBDIR}/${CMAKE_LIBRARY_ARCHITECTURE}/obs-plugins" CACHE STRING
      "OBS plugin directory relative to the installation prefix")
  endif()
  if(OBS_PLUGIN_LINUX_INSTALL_DIR STREQUAL "" OR
     IS_ABSOLUTE "${OBS_PLUGIN_LINUX_INSTALL_DIR}" OR
     OBS_PLUGIN_LINUX_INSTALL_DIR MATCHES "(^|/)\\.\\.(/|$)")
    message(FATAL_ERROR "OBS_PLUGIN_LINUX_INSTALL_DIR must be a nonempty relative package path")
  endif()
  set(OBS_PLUGIN_BINARY_DESTINATION "${OBS_PLUGIN_LINUX_INSTALL_DIR}")
  set(OBS_PLUGIN_DATA_DESTINATION
    "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/${OBS_PLUGIN_PACKAGE_ID}")
else()
  if(NOT CMAKE_LIBRARY_ARCHITECTURE)
    message(FATAL_ERROR "Linux OBS plugin packaging requires a library architecture")
  endif()
  # Ship the helper with Companion, outside OBS's search path until the installer
  # has verified the native OBS version and the helper's runtime libraries.
  set(OBS_PLUGIN_BINARY_DESTINATION "${CMAKE_INSTALL_LIBDIR}/panelive-companion/obs-plugin")
  set(OBS_PLUGIN_DATA_DESTINATION "${CMAKE_INSTALL_DATADIR}/panelive-companion/obs-plugin/data")
endif()
install(TARGETS panelive-companion-obs-plugin
  LIBRARY DESTINATION "${OBS_PLUGIN_BINARY_DESTINATION}" COMPONENT OBSPlugin
  BUNDLE DESTINATION "${OBS_PLUGIN_BINARY_DESTINATION}" COMPONENT OBSPlugin)
install(FILES LICENSE DESTINATION "${OBS_PLUGIN_DATA_DESTINATION}" COMPONENT OBSPlugin)
install(FILES data/locale/en-US.ini DESTINATION "${OBS_PLUGIN_DATA_DESTINATION}/locale"
  COMPONENT OBSPlugin)
