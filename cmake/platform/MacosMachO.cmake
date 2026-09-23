# Inspect imported native inputs before compiling the helper against an incompatible OBS/Qt.
function(studio_remote_check_macos_library library)
  execute_process(COMMAND /usr/bin/lipo "${library}" -verify_arch "${CMAKE_OSX_ARCHITECTURES}"
    RESULT_VARIABLE result ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Missing ${CMAKE_OSX_ARCHITECTURES} slice in ${library}: ${error}")
  endif()
  execute_process(COMMAND /usr/bin/otool -arch "${CMAKE_OSX_ARCHITECTURES}" -l "${library}"
    RESULT_VARIABLE result OUTPUT_VARIABLE commands ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect ${library}: ${error}")
  endif()
  string(REGEX MATCH "minos ([0-9.]+)" minimum "${commands}")
  if(NOT minimum)
    string(REGEX MATCH "LC_VERSION_MIN_MACOSX[\n ]+cmdsize [0-9]+[\n ]+version ([0-9.]+)"
      minimum "${commands}")
  endif()
  if(NOT minimum OR CMAKE_MATCH_1 VERSION_GREATER CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "${library} does not prove compatibility with macOS 13.0: ${minimum}")
  endif()
endfunction()
