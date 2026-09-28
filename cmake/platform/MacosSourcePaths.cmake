# Keep source-location diagnostics useful without publishing the builder's home path.
function(studio_remote_macos_prefix_maps output)
  set(maps)
  if(NOT "$ENV{HOME}" STREQUAL "")
    file(REAL_PATH "$ENV{HOME}" home_directory)
    list(APPEND maps "${home_directory}=/build")
  endif()
  file(REAL_PATH "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." source_root)
  list(APPEND maps "${source_root}=/src/panelive-companion")
  # vcpkg can live outside this checkout. Cover its sources and installed headers too.
  foreach(dependency_root IN ITEMS "${VCPKG_ROOT_DIR}" "${Z_VCPKG_ROOT_DIR}" "$ENV{VCPKG_ROOT}")
    if(NOT dependency_root STREQUAL "")
      file(REAL_PATH "${dependency_root}" dependency_root)
      list(APPEND maps "${dependency_root}=/src/vcpkg")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES maps)
  set(${output} "${maps}" PARENT_SCOPE)
endfunction()

function(studio_remote_configure_macos_source_paths)
  studio_remote_macos_prefix_maps(maps)
  foreach(mapping IN LISTS maps)
    add_compile_options(
      "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-ffile-prefix-map=${mapping}>"
      "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-file-prefix-map \"${mapping}\">"
      "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-debug-prefix-map \"${mapping}\">")
  endforeach()
endfunction()

function(studio_remote_macos_dependency_flags output)
  studio_remote_macos_prefix_maps(maps)
  set(flags)
  foreach(mapping IN LISTS maps)
    string(APPEND flags " \"-ffile-prefix-map=${mapping}\"")
  endforeach()
  string(STRIP "${flags}" flags)
  set(${output} "${flags}" PARENT_SCOPE)
endfunction()
