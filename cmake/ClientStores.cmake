set(PANELIVE_CLIENT_APP_STORE_URL "https://apps.apple.com/app/id6823878747" CACHE STRING
  "PaneLive Client Apple App Store URL (https://apps.apple.com/app/id followed by the numeric Apple ID)")
set(PANELIVE_CLIENT_PLAY_STORE_URL
  "https://play.google.com/store/apps/details?id=pl.clownfishweb.panelive" CACHE STRING
  "PaneLive Client Google Play Store URL")

# Empty links leave the banner visible but unavailable until the store listing is ready.
# Restrict the characters so URLs can safely be embedded in the generated C++ header.
foreach(store APP PLAY)
  set(url "${PANELIVE_CLIENT_${store}_STORE_URL}")
  if(NOT url STREQUAL "" AND NOT url MATCHES "^https://[A-Za-z0-9./?&=_%+#~-]+$")
    message(FATAL_ERROR "PANELIVE_CLIENT_${store}_STORE_URL must be an HTTPS URL or empty")
  endif()
endforeach()

configure_file("${CMAKE_CURRENT_LIST_DIR}/client_stores.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/build_config/client_stores.hpp" @ONLY)
add_library(panelive_client_stores INTERFACE)
target_include_directories(panelive_client_stores INTERFACE
  "${CMAKE_CURRENT_BINARY_DIR}/generated/build_config")
