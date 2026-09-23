set(STUDIO_REMOTE_VMIX_CATEGORY_1_COLOR "#8B0000" CACHE STRING
  "vMix input category 1 color (red, #RRGGBB)")
set(STUDIO_REMOTE_VMIX_CATEGORY_2_COLOR "#006400" CACHE STRING
  "vMix input category 2 color (green, #RRGGBB)")
set(STUDIO_REMOTE_VMIX_CATEGORY_3_COLOR "#FF8C00" CACHE STRING
  "vMix input category 3 color (orange, #RRGGBB)")
set(STUDIO_REMOTE_VMIX_CATEGORY_4_COLOR "#800080" CACHE STRING
  "vMix input category 4 color (violet, #RRGGBB)")
set(STUDIO_REMOTE_VMIX_CATEGORY_5_COLOR "#0087FF" CACHE STRING
  "vMix input category 5 color (blue, #RRGGBB)")
set(STUDIO_REMOTE_VMIX_CATEGORY_6_COLOR "#191970" CACHE STRING
  "vMix input category 6 color (dark blue, #RRGGBB)")

foreach(category RANGE 1 6)
  set(setting "STUDIO_REMOTE_VMIX_CATEGORY_${category}_COLOR")
  if(NOT "${${setting}}" MATCHES
      "^#[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]$")
    message(FATAL_ERROR "${setting} must be an RGB color in #RRGGBB format")
  endif()
endforeach()

configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/vmix_category_colors.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/build_config/studio_remote/build/vmix_category_colors.hpp"
  @ONLY)
