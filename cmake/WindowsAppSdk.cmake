include_guard(GLOBAL)

set(STUDIO_REMOTE_WINDOWS_APP_SDK_VERSION "2.4.0")
set(STUDIO_REMOTE_WINDOWS_APP_SDK_ROOT
  "${CMAKE_SOURCE_DIR}/.buildtools/windows-app-sdk/${STUDIO_REMOTE_WINDOWS_APP_SDK_VERSION}"
  CACHE PATH "Pinned Windows App SDK development package root")
option(STUDIO_REMOTE_STAGE_WINDOWS_APP_RUNTIME_INSTALLER
  "Download and stage the Windows App Runtime installer for release packaging" OFF)

function(studio_remote_extract_nuget package_id package_version package_hash output_variable)
  string(TOLOWER "${package_id}" package_name)
  set(package_root "${STUDIO_REMOTE_WINDOWS_APP_SDK_ROOT}/${package_name}.${package_version}")
  set(package_marker "${package_root}/.studio-remote-extracted")
  set(package_archive
    "${STUDIO_REMOTE_WINDOWS_APP_SDK_ROOT}/downloads/${package_name}.${package_version}.nupkg")

  if(NOT EXISTS "${package_marker}")
    file(MAKE_DIRECTORY "${STUDIO_REMOTE_WINDOWS_APP_SDK_ROOT}/downloads")
    if(NOT EXISTS "${package_archive}")
      message(STATUS "Downloading pinned ${package_id} ${package_version}")
      file(DOWNLOAD
        "https://api.nuget.org/v3-flatcontainer/${package_name}/${package_version}/${package_name}.${package_version}.nupkg"
        "${package_archive}"
        EXPECTED_HASH "SHA256=${package_hash}"
        TLS_VERIFY ON
        SHOW_PROGRESS)
    else()
      file(SHA256 "${package_archive}" actual_hash)
      string(TOUPPER "${actual_hash}" actual_hash)
      string(TOUPPER "${package_hash}" expected_hash)
      if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "Cached ${package_id} archive has an unexpected SHA-256")
      endif()
    endif()
    file(MAKE_DIRECTORY "${package_root}")
    file(ARCHIVE_EXTRACT INPUT "${package_archive}" DESTINATION "${package_root}")
    file(WRITE "${package_marker}" "${package_id} ${package_version}\n")
  endif()

  set(${output_variable} "${package_root}" PARENT_SCOPE)
endfunction()

function(studio_remote_add_windows_app_sdk)
  if(NOT WIN32)
    return()
  endif()
  if(TARGET WindowsAppSDK::WinUI)
    return()
  endif()
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The WinUI frontend currently supports x64 and ARM64 Windows builds")
  endif()

  studio_remote_extract_nuget(
    "Microsoft.WindowsAppSDK.Base" "2.0.4"
    "E3E13478C4C80C59ED5F8F89542FE49A2985DAA484753E93A5858E90C2D46A4D"
    windows_app_sdk_base)
  studio_remote_extract_nuget(
    "Microsoft.WindowsAppSDK.Foundation" "2.3.9"
    "230BC605A3FC9ED689B2117056C5274923BF58B453FA44EDDE18A168BBF628BE"
    windows_app_sdk_foundation)
  studio_remote_extract_nuget(
    "Microsoft.WindowsAppSDK.InteractiveExperiences" "2.1.6"
    "DE7B5907C63C8A79606CCC8F0D98943B154A2E62312308187E8CDC3304FF3D0B"
    windows_app_sdk_interactive)
  studio_remote_extract_nuget(
    "Microsoft.WindowsAppSDK.WinUI" "2.3.6"
    "3404F14B5AAD656024CF23EFAB49373E14BE4129B1E7D8CDD18DD26712D3C8E2"
    windows_app_sdk_winui)
  studio_remote_extract_nuget(
    "Microsoft.Web.WebView2" "1.0.3719.77"
    "2F6BE3A10A1A8D6D1FDE986AF4131DAB344F8181FFFE75590824E0F4B037ED73"
    windows_app_sdk_webview2)
  studio_remote_extract_nuget(
    "Microsoft.Windows.CppWinRT" "3.0.260818.1"
    "A8993608AE9263A8288E1A1D9EE2684A5632F4ACC59E1E89DDD86B3B889A7367"
    windows_cppwinrt)

  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(windows_app_sdk_arch arm64)
  else()
    set(windows_app_sdk_arch x64)
  endif()

  set(projection_root
    "${CMAKE_BINARY_DIR}/generated/windows-app-sdk-${STUDIO_REMOTE_WINDOWS_APP_SDK_VERSION}")
  set(projection_stamp "${projection_root}/projection.stamp")
  file(GLOB_RECURSE projection_metadata CONFIGURE_DEPENDS
    "${windows_app_sdk_foundation}/metadata/*.winmd"
    "${windows_app_sdk_interactive}/metadata/10.0.17763.0/*.winmd"
    "${windows_app_sdk_winui}/metadata/*.winmd")
  add_custom_command(
    OUTPUT "${projection_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${projection_root}"
    COMMAND "${windows_cppwinrt}/bin/cppwinrt.exe"
      -input "${windows_app_sdk_foundation}/metadata"
      -input "${windows_app_sdk_interactive}/metadata/10.0.17763.0"
      -input "${windows_app_sdk_winui}/metadata"
      -input "${windows_app_sdk_webview2}/lib/Microsoft.Web.WebView2.Core.winmd"
      -input sdk
      -output "${projection_root}"
      -base
    COMMAND "${CMAKE_COMMAND}" -E touch "${projection_stamp}"
    DEPENDS ${projection_metadata}
      "${windows_app_sdk_webview2}/lib/Microsoft.Web.WebView2.Core.winmd"
      "${windows_cppwinrt}/bin/cppwinrt.exe"
    COMMENT "Generating pinned C++/WinRT projections for WinUI 3"
    VERBATIM)
  add_custom_target(studio_remote_windows_app_sdk_projection DEPENDS "${projection_stamp}")

  add_library(studio_remote_windows_app_sdk INTERFACE)
  add_library(WindowsAppSDK::WinUI ALIAS studio_remote_windows_app_sdk)
  add_dependencies(studio_remote_windows_app_sdk studio_remote_windows_app_sdk_projection)
  target_include_directories(studio_remote_windows_app_sdk INTERFACE
    "${projection_root}"
    "${windows_app_sdk_foundation}/include"
    "${windows_app_sdk_interactive}/include"
    "${windows_app_sdk_winui}/include")
  target_link_libraries(studio_remote_windows_app_sdk INTERFACE
    "${windows_app_sdk_foundation}/lib/native/${windows_app_sdk_arch}/Microsoft.WindowsAppRuntime.Bootstrap.lib"
    runtimeobject windowsapp)

  set(STUDIO_REMOTE_WINDOWS_APP_SDK_BOOTSTRAP_DLL
    "${windows_app_sdk_foundation}/runtimes/win-${windows_app_sdk_arch}/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
    CACHE INTERNAL "Windows App SDK bootstrap DLL")

  if(STUDIO_REMOTE_STAGE_WINDOWS_APP_RUNTIME_INSTALLER)
    if(NOT windows_app_sdk_arch STREQUAL "x64")
      message(FATAL_ERROR "Windows App Runtime release staging is currently validated only for x64")
    endif()
    set(runtime_installer
      "${STUDIO_REMOTE_WINDOWS_APP_SDK_ROOT}/downloads/WindowsAppRuntimeInstall-x64.exe")
    set(runtime_installer_hash
      "851C35B0B0A59CE4C55F9171F601193322FC3413143B0DC3390EA11E14CFA7FC")
    if(NOT EXISTS "${runtime_installer}")
      file(DOWNLOAD
        "https://aka.ms/windowsappsdk/2.4/2.4.0/windowsappruntimeinstall-x64.exe"
        "${runtime_installer}"
        EXPECTED_HASH "SHA256=${runtime_installer_hash}"
        TLS_VERIFY ON
        SHOW_PROGRESS)
    else()
      file(SHA256 "${runtime_installer}" actual_runtime_installer_hash)
      string(TOUPPER "${actual_runtime_installer_hash}" actual_runtime_installer_hash)
      if(NOT actual_runtime_installer_hash STREQUAL runtime_installer_hash)
        message(FATAL_ERROR "Cached Windows App Runtime installer has an unexpected SHA-256")
      endif()
    endif()
    set(STUDIO_REMOTE_WINDOWS_APP_RUNTIME_INSTALLER "${runtime_installer}"
      CACHE INTERNAL "Pinned Windows App Runtime x64 installer")
  endif()
  set(STUDIO_REMOTE_WINDOWS_APP_SDK_LICENSE "${windows_app_sdk_foundation}/license.txt"
    CACHE INTERNAL "Windows App SDK license")
endfunction()
