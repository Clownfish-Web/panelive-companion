vcpkg_from_github(
  OUT_SOURCE_PATH SOURCE_PATH
  REPO FFmpeg/FFmpeg
  REF n9.0.1
  SHA512 41aa687cec4e93d0ffde21ea96faa48e4344fda0e9d2c9c71adfb68fedadf03beb274ec712eb1e082bf64fdcee87c55a47bb5c78d5193934be3f3f06e5e792b2
)

if(SOURCE_PATH MATCHES " ")
  message(FATAL_ERROR "FFmpeg 9 cannot build from a source path containing spaces")
endif()

if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86" OR VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
  vcpkg_find_acquire_program(NASM)
  get_filename_component(NASM_DIRECTORY "${NASM}" DIRECTORY)
  vcpkg_add_to_path("${NASM_DIRECTORY}")
endif()

if(VCPKG_HOST_IS_WINDOWS)
  vcpkg_acquire_msys(MSYS_ROOT PACKAGES automake)
  vcpkg_add_to_path("${MSYS_ROOT}/usr/bin")
  set(FFMPEG_9_BASH "${MSYS_ROOT}/usr/bin/bash.exe")
  set(FFMPEG_9_MAKE "${MSYS_ROOT}/usr/bin/make.exe")
else()
  find_program(FFMPEG_9_BASH bash REQUIRED)
  find_program(FFMPEG_9_MAKE make REQUIRED)
endif()

set(FFMPEG_9_OPTIONS
  --disable-autodetect
  --disable-doc
  --disable-ffmpeg
  --disable-ffplay
  --disable-ffprobe
  --disable-avdevice
  --disable-avfilter
  --disable-swresample
  --disable-alsa
  --disable-bzlib
  --disable-iconv
  --disable-lzma
  --disable-opengl
  --disable-openssl
  --disable-sdl2
  --disable-vulkan
  --disable-zlib
  --enable-avcodec
  --enable-avformat
  --enable-swscale
  --enable-pic
  --enable-runtime-cpudetect)

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
  list(APPEND FFMPEG_9_OPTIONS --disable-static --enable-shared)
else()
  list(APPEND FFMPEG_9_OPTIONS --disable-shared --enable-static)
endif()

if(VCPKG_TARGET_IS_WINDOWS)
  list(APPEND FFMPEG_9_OPTIONS --enable-w32threads --target-os=win32 --toolchain=msvc)
elseif(VCPKG_TARGET_IS_LINUX)
  list(APPEND FFMPEG_9_OPTIONS --enable-pthreads --target-os=linux)
elseif(VCPKG_TARGET_IS_OSX)
  list(APPEND FFMPEG_9_OPTIONS --enable-pthreads --target-os=darwin)
  # This overlay invokes configure directly, so it must forward the triplet's Apple target.
  # Otherwise configure silently compiles for the build host's OS and architecture.
  list(LENGTH VCPKG_OSX_ARCHITECTURES FFMPEG_9_OSX_ARCHITECTURE_COUNT)
  if(FFMPEG_9_OSX_ARCHITECTURE_COUNT GREATER 1)
    message(FATAL_ERROR "FFmpeg macOS requires separate per-architecture builds before lipo")
  endif()
  if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(FFMPEG_9_OSX_ARCHITECTURE arm64)
  elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(FFMPEG_9_OSX_ARCHITECTURE x86_64)
  else()
    message(FATAL_ERROR "FFmpeg macOS supports arm64 or x64")
  endif()
  set(FFMPEG_9_OSX_FLAGS "-arch ${FFMPEG_9_OSX_ARCHITECTURE}")
  if(VCPKG_OSX_DEPLOYMENT_TARGET)
    string(APPEND FFMPEG_9_OSX_FLAGS " -mmacosx-version-min=${VCPKG_OSX_DEPLOYMENT_TARGET}")
  endif()
  list(APPEND FFMPEG_9_OPTIONS
    "--arch=${FFMPEG_9_OSX_ARCHITECTURE}"
    "--extra-cflags=${FFMPEG_9_OSX_FLAGS}"
    "--extra-ldflags=${FFMPEG_9_OSX_FLAGS}")
elseif(VCPKG_TARGET_IS_IOS)
  list(APPEND FFMPEG_9_OPTIONS --enable-pthreads)
else()
  message(FATAL_ERROR "FFmpeg 9 overlay does not support this target platform")
endif()

function(studio_remote_build_ffmpeg_9 install_prefix)
  vcpkg_execute_required_process(
    COMMAND "${FFMPEG_9_BASH}" "${SOURCE_PATH}/configure" "--prefix=${install_prefix}"
      ${FFMPEG_9_OPTIONS} ${ARGN}
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME configure-${TARGET_TRIPLET})
  vcpkg_execute_required_process(
    COMMAND "${FFMPEG_9_MAKE}" "-j${VCPKG_CONCURRENCY}"
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME build-${TARGET_TRIPLET})
  vcpkg_execute_required_process(
    COMMAND "${FFMPEG_9_MAKE}" install
    WORKING_DIRECTORY "${SOURCE_PATH}"
    LOGNAME install-${TARGET_TRIPLET})
endfunction()

studio_remote_build_ffmpeg_9("${CURRENT_PACKAGES_DIR}" --enable-optimizations)
vcpkg_execute_required_process(
  COMMAND "${FFMPEG_9_MAKE}" distclean
  WORKING_DIRECTORY "${SOURCE_PATH}"
  LOGNAME clean-${TARGET_TRIPLET})
studio_remote_build_ffmpeg_9("${CURRENT_PACKAGES_DIR}/debug" --disable-optimizations --enable-debug)

if(VCPKG_TARGET_IS_WINDOWS)
  foreach(FFMPEG_9_LIBRARY IN ITEMS avcodec avformat avutil swscale)
    file(RENAME
      "${CURRENT_PACKAGES_DIR}/bin/${FFMPEG_9_LIBRARY}.lib"
      "${CURRENT_PACKAGES_DIR}/lib/${FFMPEG_9_LIBRARY}.lib")
    file(RENAME
      "${CURRENT_PACKAGES_DIR}/debug/bin/${FFMPEG_9_LIBRARY}.lib"
      "${CURRENT_PACKAGES_DIR}/debug/lib/${FFMPEG_9_LIBRARY}.lib")
  endforeach()
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/doc")
vcpkg_fixup_pkgconfig()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.LGPLv2.1")
