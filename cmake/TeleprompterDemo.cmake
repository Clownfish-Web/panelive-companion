# Embed the versioned UTF-8 script so every installer carries it with the application.
set(teleprompter_demo_source "${CMAKE_CURRENT_LIST_DIR}/../docs/teleprompter/solo-travel.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${teleprompter_demo_source}")
file(READ "${teleprompter_demo_source}" teleprompter_demo_hex HEX)
# Byte escapes preserve UTF-8 independently of the compiler's source/execution encoding.
string(REGEX REPLACE "(..)" "\\\\x\\1" STUDIO_REMOTE_TELEPROMPTER_DEMO "${teleprompter_demo_hex}")
string(REPLACE "\\x0a" "\\x0a\"\n\"" STUDIO_REMOTE_TELEPROMPTER_DEMO "${STUDIO_REMOTE_TELEPROMPTER_DEMO}")
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/teleprompter_demo.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/build_config/studio_remote/build/teleprompter_demo.hpp"
  @ONLY)
