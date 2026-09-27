# Distribution selection changes packaging/linker policy; provider/runtime selection stays portable.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(STUDIO_REMOTE_LINUX_DISTRIBUTION "DEBIAN" CACHE STRING
    "Linux package layout: DEBIAN or ARCH")
  set_property(CACHE STUDIO_REMOTE_LINUX_DISTRIBUTION PROPERTY STRINGS DEBIAN ARCH)
  if(NOT STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "DEBIAN" AND
     NOT STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "ARCH")
    message(FATAL_ERROR "STUDIO_REMOTE_LINUX_DISTRIBUTION must be DEBIAN or ARCH")
  endif()
  if(STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "ARCH")
    # Match Arch's immediate binding/RELRO policy and discard unused transitive SDK libraries.
    # Apply to the companion and the separately linked OBS module without changing their ABI.
    add_link_options("LINKER:-z,relro" "LINKER:-z,now" "LINKER:--as-needed")
  endif()
endif()
