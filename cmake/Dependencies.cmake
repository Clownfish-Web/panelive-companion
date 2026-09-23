find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(LibDataChannel CONFIG REQUIRED)
find_package(unofficial-libvpx CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(CivetWeb CONFIG REQUIRED)
if(APPLE)
  # The pinned CivetWeb 1.16 export uses archive paths, while the other transports use targets.
  # Give each dependency one identity for Apple linker deduplication. Remove this adaptation
  # when the pinned CivetWeb export uses imported dependency targets itself.
  find_package(ZLIB REQUIRED)
  set_property(TARGET civetweb::civetweb PROPERTY INTERFACE_LINK_LIBRARIES
    OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS} ZLIB::ZLIB)
endif()
# libsrt is the vMix SRT receiver transport and is only acquired for Windows in vcpkg.json.
if(WIN32)
  find_path(SRT_INCLUDE_DIR NAMES srt/srt.h REQUIRED)
  find_library(SRT_LIBRARY NAMES srt REQUIRED)
endif()
find_path(FFMPEG_INCLUDE_DIR NAMES libavformat/avformat.h REQUIRED)
find_library(FFMPEG_AVCODEC_LIBRARY NAMES avcodec REQUIRED)
find_library(FFMPEG_AVFORMAT_LIBRARY NAMES avformat REQUIRED)
find_library(FFMPEG_AVUTIL_LIBRARY NAMES avutil REQUIRED)
find_library(FFMPEG_SWSCALE_LIBRARY NAMES swscale REQUIRED)
find_path(QRENCODE_INCLUDE_DIR qrencode.h REQUIRED)
find_library(QRENCODE_LIBRARY NAMES qrencode REQUIRED)
