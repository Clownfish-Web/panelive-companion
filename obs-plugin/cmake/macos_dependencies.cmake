set(OBS_APP_PATH "/Applications/OBS.app" CACHE PATH "Matching installed OBS application")
set(OBS_DEPENDENCIES_DIR "" CACHE PATH "Matching OBS native dependency package")
foreach(required_path IN ITEMS
  "${OBS_SOURCE_DIR}/libobs/obs-module.h"
  "${OBS_APP_PATH}/Contents/Frameworks/libobs.framework/libobs"
  "${OBS_APP_PATH}/Contents/Frameworks/obs-frontend-api.dylib"
  "${OBS_QT_DIR}/lib/cmake/Qt6/Qt6Config.cmake"
  "${OBS_DEPENDENCIES_DIR}/include/simde/x86/sse2.h")
  if(NOT EXISTS "${required_path}")
    message(FATAL_ERROR "Required macOS OBS development input is missing: ${required_path}")
  endif()
endforeach()
list(PREPEND CMAKE_PREFIX_PATH "${OBS_QT_DIR}")
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Widgets)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/platform/MacosMachO.cmake")
foreach(library IN ITEMS
    "${OBS_APP_PATH}/Contents/MacOS/OBS"
    "${OBS_APP_PATH}/Contents/Frameworks/libobs.framework/libobs"
    "${OBS_APP_PATH}/Contents/Frameworks/obs-frontend-api.dylib"
    "${OBS_QT_DIR}/lib/QtCore.framework/QtCore"
    "${OBS_QT_DIR}/lib/QtGui.framework/QtGui"
    "${OBS_QT_DIR}/lib/QtWidgets.framework/QtWidgets")
  studio_remote_check_macos_library("${library}")
endforeach()
add_library(OBS::libobs SHARED IMPORTED)
set_target_properties(OBS::libobs PROPERTIES
  IMPORTED_LOCATION "${OBS_APP_PATH}/Contents/Frameworks/libobs.framework/libobs"
  INTERFACE_INCLUDE_DIRECTORIES "${OBS_SOURCE_DIR}/libobs;${OBS_DEPENDENCIES_DIR}/include")
add_library(OBS::obs-frontend-api SHARED IMPORTED)
set_target_properties(OBS::obs-frontend-api PROPERTIES
  IMPORTED_LOCATION "${OBS_APP_PATH}/Contents/Frameworks/obs-frontend-api.dylib"
  INTERFACE_INCLUDE_DIRECTORIES "${OBS_SOURCE_DIR}/frontend/api")
