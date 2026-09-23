# Include before project(): deployment and architecture must reach compiler and vcpkg probes.
# Distribution uses independent native packages, including their matching OBS helper.
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version")
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET VERSION_EQUAL "13.0")
  message(FATAL_ERROR "The macOS Companion and helper require deployment target 13.0")
endif()
if(NOT CMAKE_OSX_ARCHITECTURES MATCHES "^(arm64|x86_64)$")
  message(FATAL_ERROR
    "Select one native architecture: arm64 or baseline x86_64. "
    "Use macos-arm64-release or macos-x64-release; universal builds are not supported.")
endif()
set(STUDIO_REMOTE_MACOS_SWIFT_TARGET
  "${CMAKE_OSX_ARCHITECTURES}-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}")
