function(studio_remote_add_macos_bundle_resource target source destination)
  target_sources(${target} PRIVATE "${source}")
  set_source_files_properties("${source}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/${destination}")
  # A copied resource changes the signature even when no C++/Swift source changed.
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${source}")
endfunction()

function(studio_remote_configure_macos_bundle target)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "A macOS application bundle requires an explicit deployment target")
  endif()
  set(STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY "-" CACHE STRING
    "Companion signing identity; '-' is an ad-hoc development build, not a distribution identity")
  if(STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY STREQUAL "")
    message(FATAL_ERROR "Select a macOS signing identity or '-' for development")
  endif()
  set(plist "${PROJECT_BINARY_DIR}/generated/macos/Info.plist")
  configure_file("${PROJECT_SOURCE_DIR}/platform/macos/packaging/Info.plist.in" "${plist}" @ONLY)
  set_target_properties(${target} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${plist}"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/usr/lib/swift"
    INSTALL_RPATH_USE_LINK_PATH FALSE)
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${plist}")
  # Adding an older resource still changes the sealed bundle even if no input is newer
  # than its executable. Changes to this resource list must rerun the signing step.
  set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
  add_dependencies(${target} studio_remote_macos_icon)
  studio_remote_add_macos_bundle_resource(${target}
    "${STUDIO_REMOTE_MACOS_ICON_DIRECTORY}/${PANELIVE_COMPANION_PACKAGE_ID}.icns" "")
  foreach(resource IN ITEMS LICENSE NOTICE THIRD_PARTY_NOTICES.md
      README.md docs/BUILDING.md docs/RUNNING.md docs/DOWNLOADS.md docs/MACOS_RELEASE.md
      obs-plugin/LICENSE packaging/macos/INSTALL_OBS_PLUGIN_MACOS.md
      releases/0.1.0-beta.1/release.json)
    get_filename_component(resource_directory "${resource}" DIRECTORY)
    studio_remote_add_macos_bundle_resource(${target} "${PROJECT_SOURCE_DIR}/${resource}"
      "share/${PANELIVE_COMPANION_PACKAGE_ID}/${resource_directory}")
  endforeach()
  set(timestamp --timestamp)
  if(STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY STREQUAL "-")
    set(timestamp --timestamp=none)
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND /usr/bin/codesign --force --sign "${STUDIO_REMOTE_MACOS_CODESIGN_IDENTITY}"
      --identifier "${PANELIVE_COMPANION_PACKAGE_ID}"
      --options runtime ${timestamp} "$<TARGET_BUNDLE_DIR:${target}>"
    COMMAND /usr/bin/codesign --verify --strict "$<TARGET_BUNDLE_DIR:${target}>"
    VERBATIM)
endfunction()
