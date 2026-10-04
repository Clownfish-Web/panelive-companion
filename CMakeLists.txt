cmake_minimum_required(VERSION 3.28)
if(CMAKE_HOST_APPLE)
  include(cmake/platform/MacosDeployment.cmake)
  # Apple ld resolves previously seen archives. Preserve dependency order without repeats.
  # Set these before project(), when CMake initializes the Apple linker capabilities.
  if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
  endif()
  if(POLICY CMP0179)
    cmake_policy(SET CMP0179 NEW)
  endif()
endif()
include(cmake/Version.cmake)
project(panelive-companion VERSION ${STUDIO_REMOTE_VERSION}
  DESCRIPTION "${STUDIO_REMOTE_PRODUCT_NAME}" LANGUAGES C CXX)
if(APPLE)
  include(cmake/platform/MacosSourcePaths.cmake)
  studio_remote_configure_macos_source_paths()
endif()

set(PANELIVE_COMPANION_PACKAGE_ID "panelive-companion")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(LinuxDistribution)
include(Dependencies)
include(RemoteTallyService)
if(NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(VmixCategoryColors)
endif()
if(WIN32)
  include(WindowsAppSdk)
  studio_remote_add_windows_app_sdk()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(STUDIO_REMOTE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(STUDIO_REMOTE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(STUDIO_REMOTE_ENABLE_PROVIDER_OBS "Build the implemented OBS Studio provider" ON)
include(VmixOptions)
option(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS "Build the Streamlabs Desktop provider" OFF)
option(STUDIO_REMOTE_ENABLE_PROVIDER_XSPLIT "Build the XSplit Broadcaster provider" OFF)
option(STUDIO_REMOTE_ENABLE_PROVIDER_WIRECAST "Build the Wirecast provider" OFF)

if(NOT STUDIO_REMOTE_ENABLE_PROVIDER_OBS)
  message(FATAL_ERROR
    "The current production composition requires STUDIO_REMOTE_ENABLE_PROVIDER_OBS=ON")
endif()
if(STUDIO_REMOTE_ENABLE_SANITIZERS AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "STUDIO_REMOTE_ENABLE_SANITIZERS is supported only by the Linux presets")
endif()
if(NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(STUDIO_REMOTE_ENABLE_VMIX_NDI AND NOT STUDIO_REMOTE_ENABLE_PROVIDER_VMIX)
    message(FATAL_ERROR "vMix NDI capture requires STUDIO_REMOTE_ENABLE_PROVIDER_VMIX=ON")
  endif()
  if(NOT STUDIO_REMOTE_VMIX_VIDEO_ADAPTER STREQUAL "SRT" AND
     NOT STUDIO_REMOTE_VMIX_VIDEO_ADAPTER STREQUAL "NDI")
    message(FATAL_ERROR "STUDIO_REMOTE_VMIX_VIDEO_ADAPTER must be SRT or NDI")
  endif()
  if(STUDIO_REMOTE_VMIX_VIDEO_ADAPTER STREQUAL "NDI" AND NOT STUDIO_REMOTE_ENABLE_VMIX_NDI)
    message(FATAL_ERROR "The NDI vMix video adapter requires STUDIO_REMOTE_ENABLE_VMIX_NDI=ON")
  endif()
  if(NOT STUDIO_REMOTE_VMIX_AUDIO_ADAPTER STREQUAL "NONE" AND
     NOT STUDIO_REMOTE_VMIX_AUDIO_ADAPTER STREQUAL "NDI" AND
     NOT STUDIO_REMOTE_VMIX_AUDIO_ADAPTER STREQUAL "OMT")
    message(FATAL_ERROR "STUDIO_REMOTE_VMIX_AUDIO_ADAPTER must be NONE, NDI, or OMT")
  endif()
  if(STUDIO_REMOTE_VMIX_AUDIO_ADAPTER STREQUAL "NDI" AND NOT STUDIO_REMOTE_ENABLE_VMIX_NDI)
    message(FATAL_ERROR "The NDI vMix audio adapter requires STUDIO_REMOTE_ENABLE_VMIX_NDI=ON")
  endif()
endif()

add_subdirectory(providers/obs)
if(NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(providers/vmix)
endif()
add_subdirectory(providers/streamlabs)
add_subdirectory(providers/teleprompter)
add_subdirectory(providers/xsplit)
add_subdirectory(providers/wirecast)

add_library(studio_remote_protocol INTERFACE)
target_include_directories(studio_remote_protocol INTERFACE generated/protocol include)
target_compile_definitions(studio_remote_protocol INTERFACE
  STUDIO_REMOTE_PROTOCOL_VERSION="${STUDIO_REMOTE_PROTOCOL_VERSION}")

add_library(studio_remote_core STATIC
  core/tally/definition.cpp
  core/tally/publication.cpp
  core/tally/resolver.cpp
  core/teleprompter/script.cpp
  core/teleprompter/progress.cpp
  core/teleprompter/session.cpp
  core/teleprompter/script_catalog.cpp
  core/teleprompter/memory_session_repository.cpp
  core/transport/request_admission.cpp
  core/transport/client_frame_budget.cpp
  core/configuration/stored_password.cpp
  core/discovery/lan_address_selection.cpp
  core/discovery/companion_service_record.cpp
  core/observability/endurance_event_log.cpp
  core/observability/command_diagnostics.cpp
  core/observability/debug_log_files.cpp
  core/observability/endurance_snapshot_source.cpp
  core/observability/endurance_snapshot_registration.cpp
  core/observability/obs_endurance_snapshot.cpp
  core/media/audio_diagnostic_peak_window.cpp
  core/media/audio_diagnostic_session_log.cpp
  core/media/audio_level_telemetry.cpp
  core/media/audio_pipeline_hardening.cpp
  core/media/audio_playout_controller.cpp
  core/media/thumbnail_encoding.cpp
  core/media/video_rtp_timeline.cpp
  core/ipc/audio_monitor_protocol.cpp
  core/ipc/audio_monitor_reader.cpp
  core/ipc/source_thumbnail_protocol.cpp
  core/ipc/video_capture_demand_publisher.cpp
  core/ipc/video_frame_reader.cpp
  core/credentials/audit_store.cpp
  core/credentials/pairing_manager.cpp
  core/input/hotkey.cpp
  core/provider/provider_catalog.cpp
  core/provider/provider_selection.cpp
  core/state/audio_monitoring_mode.cpp
  core/state/command_state_snapshot.cpp
  core/state/source_membership.cpp
  core/state/state_source_membership.cpp
  core/state/state_store.cpp)
target_include_directories(studio_remote_core PUBLIC include)
target_link_libraries(studio_remote_core PUBLIC studio_remote_protocol nlohmann_json::nlohmann_json
  unofficial::sqlite3::sqlite3)
if(STUDIO_REMOTE_ENABLE_PROVIDER_VMIX)
  target_compile_definitions(studio_remote_core PUBLIC STUDIO_REMOTE_ENABLE_PROVIDER_VMIX=1)
endif()
if(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
  target_compile_definitions(studio_remote_core PUBLIC STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS=1)
endif()
studio_remote_set_warnings(studio_remote_core)

add_library(studio_remote_tally_protocol STATIC
  protocol/tally/protocol.cpp
  protocol/tally/descriptor.cpp)
target_link_libraries(studio_remote_tally_protocol PUBLIC studio_remote_core)
studio_remote_set_warnings(studio_remote_tally_protocol)

add_library(studio_remote_tally_storage STATIC
  platform/shared/tally/sqlite_publication_repository.cpp
  platform/shared/tally/publication_schema.cpp
  platform/shared/tally/publication_rows.cpp
  platform/shared/tally/publication_admission.cpp
  platform/shared/tally/publication_outbox.cpp)
target_link_libraries(studio_remote_tally_storage PUBLIC studio_remote_tally_protocol
  PRIVATE unofficial::sqlite3::sqlite3)
studio_remote_set_warnings(studio_remote_tally_storage)

add_library(studio_remote_teleprompter_storage STATIC
  platform/shared/teleprompter/sqlite_teleprompter_repository.cpp
  platform/shared/teleprompter/session_device_source.cpp)
target_link_libraries(studio_remote_teleprompter_storage PUBLIC studio_remote_core
  PRIVATE unofficial::sqlite3::sqlite3)
studio_remote_set_warnings(studio_remote_teleprompter_storage)

add_library(studio_remote_obs_adapter STATIC
  core/obs_adapter/obs_connection_callbacks.cpp
  core/obs_adapter/obs_diagnostic_events.cpp
  core/obs_adapter/obs_pending_requests.cpp
  core/obs_adapter/obs_rpc_request_exchange.cpp
  core/obs_adapter/obs_wire_protocol.cpp
  core/obs_adapter/obs_performance_snapshot.cpp
  core/obs_adapter/obs_quick_transition_identities.cpp
  core/obs_adapter/obs_quick_transition_catalog.cpp
  core/obs_adapter/obs_quick_transition_protocol.cpp
  core/obs_adapter/obs_event_reader.cpp
  core/obs_adapter/obs_hotkey_catalog.cpp
  core/obs_adapter/obs_hotkey_sequence.cpp
  core/obs_adapter/obs_protocol.cpp
  core/obs_adapter/obs_reconnect.cpp
  core/obs_adapter/obs_request_arbiter.cpp)
target_include_directories(studio_remote_obs_adapter PUBLIC include)
target_link_libraries(studio_remote_obs_adapter PUBLIC studio_remote_core OpenSSL::Crypto)
studio_remote_set_warnings(studio_remote_obs_adapter)

add_library(studio_remote_transport STATIC
  core/media/media_policy.cpp
  core/transport/audio_egress_observer.cpp
  core/transport/audio_nack_responder.cpp
  core/transport/audio_rtcp_feedback_handler.cpp
  core/transport/libdatachannel_transport.cpp
  platform/shared/media/vp8_video_encoder.cpp
  core/transport/rtp_ssrc_allocator.cpp
  core/transport/sdp_offer_limits.cpp
  core/transport/webrtc_audio_encoder.cpp
  core/transport/webrtc_audio_master.cpp
  core/transport/webrtc_audio_sender.cpp
  core/transport/webrtc_offer.cpp
  core/transport/webrtc_pending_offer.cpp
  core/transport/webrtc_remote_offer.cpp
  core/transport/vp8_rtp_packetizer.cpp)
target_include_directories(studio_remote_transport PUBLIC include PRIVATE core/transport)
target_link_libraries(studio_remote_transport PUBLIC studio_remote_core LibDataChannel::LibDataChannel
  unofficial::libvpx::libvpx Opus::opus)
studio_remote_set_warnings(studio_remote_transport)

add_library(studio_remote_application STATIC
  ui/shared/management_approval_dispatcher.cpp
  ui/shared/management_status_snapshot_builder.cpp
  ui/shared/management_status_rows.cpp
  ui/shared/management_clients.cpp
  ui/shared/management_client_notifications.cpp
  ui/shared/management_adapter_rows.cpp
  core/application/audio_mixer_roster.cpp
  core/application/management_controller.cpp
  core/application/management_tally_permissions.cpp
  core/application/teleprompter_controller.cpp
  core/application/teleprompter_sharing_controller.cpp
  core/application/tally_publication_service.cpp
  core/application/endurance_logging_setting.cpp
  core/application/endurance_process_log.cpp
  core/application/gateway.cpp
  core/application/client_event_relay.cpp
  core/application/client_delivery_queue.cpp
  core/application/countdown_event_policy.cpp
  core/application/gateway_lifecycle.cpp
  core/application/hotkey_service.cpp
  core/application/critical_live_control_service.cpp
  core/application/gateway_command_processor.cpp
  core/application/gateway_collection_commands.cpp
  core/application/obs_state_reconciler.cpp
  core/application/obs_configuration_service.cpp
  core/application/performance_sampler.cpp
  core/application/scene_item_properties.cpp)
target_sources(studio_remote_application PRIVATE core/application/thumbnail_service.cpp)
target_include_directories(studio_remote_application PUBLIC core/application)
target_link_libraries(studio_remote_application PUBLIC
  studio_remote_core
  studio_remote_obs_adapter)
studio_remote_set_warnings(studio_remote_application)

add_library(studio_remote_tally_routes STATIC
  app/gateway_main/tally_publication_routes.cpp
  app/gateway_main/tally_management_callbacks.cpp)
target_link_libraries(studio_remote_tally_routes PUBLIC
  studio_remote_application studio_remote_tally_protocol)
studio_remote_set_warnings(studio_remote_tally_routes)

add_library(studio_remote_pairing_qr STATIC
  ui/shared/pairing_qr.cpp ui/shared/pairing_preview.cpp)
target_include_directories(studio_remote_pairing_qr PRIVATE ${QRENCODE_INCLUDE_DIR})
target_link_libraries(studio_remote_pairing_qr PRIVATE ${QRENCODE_LIBRARY})
studio_remote_set_warnings(studio_remote_pairing_qr)

add_library(studio_remote_audio_monitor_worker STATIC
  platform/shared/capture/audio_monitor_worker.cpp)
target_link_libraries(studio_remote_audio_monitor_worker PUBLIC studio_remote_core)
studio_remote_set_warnings(studio_remote_audio_monitor_worker)

add_library(studio_remote_video_capture_worker STATIC
  platform/shared/capture/video_capture_worker.cpp)
target_link_libraries(studio_remote_video_capture_worker PUBLIC studio_remote_core)
studio_remote_set_warnings(studio_remote_video_capture_worker)

if(WIN32)
  add_library(studio_remote_platform_windows STATIC
    platform/windows/ipc/windows_ipc_mapping.cpp
    platform/windows/capture/windows_audio_diagnostic_player.cpp
    platform/windows/capture/windows_audio_rate_adjustment.cpp
    platform/windows/capture/windows_audio_monitor.cpp
    platform/windows/capture/windows_audio_monitor_ipc.cpp
    platform/windows/capture/windows_audio_mixer_roster.cpp
    platform/windows/capture/windows_capture.cpp
    platform/windows/capture/windows_video_capture_demand.cpp
    platform/windows/capture/windows_video_frames.cpp
    platform/windows/capture/windows_scene_item_properties.cpp
    platform/windows/capture/windows_source_media_inspector.cpp
    platform/windows/capture/windows_thumbnail_capture.cpp
    platform/windows/capture/windows_thumbnail_mailbox.cpp
    platform/windows/capture/windows_thumbnail_encoder.cpp
    platform/windows/capture/windows_vmix_thumbnail_capture.cpp
    platform/windows/credentials/windows_credential_store.cpp
    platform/windows/credentials/windows_crypto.cpp
    platform/windows/credentials/windows_tls_identity.cpp
    platform/windows/credentials/windows_private_tls_storage.cpp
    platform/windows/platform_services/windows_dns_sd.cpp
    platform/windows/platform_services/windows_host_diagnostics.cpp
    platform/windows/platform_services/windows_hotkey_executor.cpp
    platform/windows/platform_services/windows_lan_address.cpp
    platform/windows/platform_services/windows_listener_port.cpp
    platform/windows/platform_services/windows_management_view_adapter.cpp
    platform/windows/platform_services/windows_obs_quick_transition_catalog.cpp
    platform/shared/obs/obs_saved_quick_transition_catalog.cpp
    platform/windows/platform_services/windows_obs_quick_transition_control.cpp
    platform/windows/platform_services/windows_obs_runtime_capabilities.cpp
    platform/windows/platform_services/windows_port_diagnostics.cpp
    platform/windows/platform_services/windows_config.cpp
    platform/shared/configuration/configuration_stream.cpp
    platform/windows/platform_services/windows_platform_services.cpp
    platform/windows/platform_services/windows_realtime_audio_services.cpp
    platform/windows/platform_services/windows_shutdown_signal.cpp
    platform/windows/platform_services/windows_sign_in_startup.cpp
    platform/windows/platform_services/windows_single_instance.cpp
    platform/windows/platform_services/winhttp_obs_client.cpp
    platform/windows/platform_services/winhttp_obs_socket_connection.cpp
    platform/windows/platform_services/windows_obs_websocket.cpp)
  target_sources(studio_remote_platform_windows PRIVATE
    platform/windows/platform_services/windows_tally_health_probe.cpp
    platform/windows/platform_services/windows_tally_health_request.cpp)
  target_link_libraries(studio_remote_platform_windows PRIVATE studio_remote_tally_protocol)
  if(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    target_sources(studio_remote_platform_windows PRIVATE
      platform/windows/platform_services/windows_streamlabs_installations.cpp
      platform/windows/platform_services/windows_streamlabs_named_pipe_transport.cpp)
    target_link_libraries(studio_remote_platform_windows PUBLIC studio_remote_streamlabs_adapter)
  endif()
  if(STUDIO_REMOTE_ENABLE_PROVIDER_VMIX)
    target_sources(studio_remote_platform_windows PRIVATE
      platform/windows/capture/srt_sequential_stream.cpp
      platform/windows/capture/windows_mpeg_ts_video_decoder.cpp
      platform/windows/capture/windows_vmix_srt_capture.cpp
      platform/windows/capture/windows_vmix_input_category_source.cpp
      platform/windows/capture/windows_vmix_output_geometry_source.cpp
      platform/windows/capture/vmix_audio_stream_supervisor.cpp
      platform/windows/capture/vmix_video_stream_supervisor.cpp
      platform/windows/platform_services/windows_production_installations.cpp
      platform/windows/platform_services/windows_vmix_streaming_destination_source.cpp
      platform/windows/platform_services/winhttp_vmix_transport.cpp)
    target_sources(studio_remote_platform_windows PRIVATE
      platform/windows/platform_services/windows_vmix_telestrator_address.cpp
      platform/windows/platform_services/windows_vmix_telestrator_request.cpp
      platform/windows/platform_services/windows_vmix_telestrator_probe.cpp)
    target_link_libraries(studio_remote_platform_windows PUBLIC studio_remote_vmix_adapter)
    if(STUDIO_REMOTE_VMIX_VIDEO_ADAPTER STREQUAL "SRT")
      target_compile_definitions(studio_remote_platform_windows PUBLIC
        STUDIO_REMOTE_VMIX_VIDEO_ADAPTER_SRT=1)
    else()
      target_compile_definitions(studio_remote_platform_windows PUBLIC
        STUDIO_REMOTE_VMIX_VIDEO_ADAPTER_NDI=1)
    endif()
    if(STUDIO_REMOTE_VMIX_AUDIO_ADAPTER STREQUAL "NDI")
      target_compile_definitions(studio_remote_platform_windows PUBLIC
        STUDIO_REMOTE_VMIX_AUDIO_ADAPTER_NDI=1)
    elseif(STUDIO_REMOTE_VMIX_AUDIO_ADAPTER STREQUAL "OMT")
      set(STUDIO_REMOTE_OMT_PLATFORM_DIRECTORY "Winx64")
      if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        set(STUDIO_REMOTE_OMT_PLATFORM_DIRECTORY "Winarm64")
      endif()
      set(STUDIO_REMOTE_OMT_LIBRARY_DIRECTORY
        "${STUDIO_REMOTE_OMT_SDK_DIR}/Libraries/${STUDIO_REMOTE_OMT_PLATFORM_DIRECTORY}")
      find_path(STUDIO_REMOTE_OMT_INCLUDE_DIR libomt.h
        HINTS "${STUDIO_REMOTE_OMT_LIBRARY_DIRECTORY}" NO_DEFAULT_PATH)
      find_library(STUDIO_REMOTE_OMT_LIBRARY libomt
        HINTS "${STUDIO_REMOTE_OMT_LIBRARY_DIRECTORY}" NO_DEFAULT_PATH)
      set(STUDIO_REMOTE_OMT_RUNTIME
        "${STUDIO_REMOTE_OMT_LIBRARY_DIRECTORY}/libomt.dll")
      set(STUDIO_REMOTE_OMT_LICENSE "${STUDIO_REMOTE_OMT_SDK_DIR}/LICENSE.txt")
      if(NOT STUDIO_REMOTE_OMT_INCLUDE_DIR OR NOT STUDIO_REMOTE_OMT_LIBRARY OR
         NOT EXISTS "${STUDIO_REMOTE_OMT_RUNTIME}" OR NOT EXISTS "${STUDIO_REMOTE_OMT_LICENSE}")
        message(FATAL_ERROR
          "The OMT audio adapter requires the official SDK at STUDIO_REMOTE_OMT_SDK_DIR")
      endif()
      target_sources(studio_remote_platform_windows PRIVATE
        platform/windows/capture/windows_omt_audio_stream_adapter.cpp
        platform/windows/capture/windows_omt_runtime.cpp)
      target_include_directories(studio_remote_platform_windows PRIVATE
        "${STUDIO_REMOTE_OMT_INCLUDE_DIR}")
      target_link_libraries(studio_remote_platform_windows PUBLIC "${STUDIO_REMOTE_OMT_LIBRARY}")
      target_compile_definitions(studio_remote_platform_windows PUBLIC
        STUDIO_REMOTE_VMIX_AUDIO_ADAPTER_OMT=1)
    endif()
  endif()
  if(STUDIO_REMOTE_ENABLE_VMIX_NDI)
    find_path(STUDIO_REMOTE_NDI_INCLUDE_DIR Processing.NDI.Lib.h
      HINTS "${STUDIO_REMOTE_NDI_SDK_DIR}/Include" "${STUDIO_REMOTE_NDI_SDK_DIR}/include")
    if(NOT STUDIO_REMOTE_NDI_INCLUDE_DIR)
      message(FATAL_ERROR
        "STUDIO_REMOTE_ENABLE_VMIX_NDI requires an NDI SDK root with receiver headers")
    endif()
    target_sources(studio_remote_platform_windows PRIVATE
      platform/windows/capture/ndi_video_frame_normalizer.cpp
      platform/windows/capture/windows_ndi_audio_stream_adapter.cpp
      platform/windows/capture/windows_ndi_capture.cpp
      platform/windows/capture/windows_ndi_runtime.cpp)
    target_include_directories(studio_remote_platform_windows PRIVATE
      "${STUDIO_REMOTE_NDI_INCLUDE_DIR}")
    target_compile_definitions(studio_remote_platform_windows PUBLIC STUDIO_REMOTE_HAS_NDI=1)
  endif()
  target_include_directories(studio_remote_platform_windows PUBLIC include PRIVATE platform/windows)
  target_compile_definitions(studio_remote_platform_windows PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
  target_link_libraries(studio_remote_platform_windows PUBLIC studio_remote_core studio_remote_obs_adapter
    studio_remote_audio_monitor_worker studio_remote_video_capture_worker
    OpenSSL::SSL OpenSSL::Crypto ${SRT_LIBRARY} ${FFMPEG_AVCODEC_LIBRARY}
    ${FFMPEG_AVFORMAT_LIBRARY} ${FFMPEG_AVUTIL_LIBRARY} ${FFMPEG_SWSCALE_LIBRARY}
    winhttp wintrust version crypt32 bcrypt d3d11 dxgi windowsapp
    windowscodecs dnsapi iphlpapi psapi ws2_32 shlwapi shell32 ole32 xmllite user32
    avrt advapi32 mfplat mfuuid)
  target_include_directories(studio_remote_platform_windows PRIVATE ${SRT_INCLUDE_DIR})
  target_include_directories(studio_remote_platform_windows PRIVATE ${FFMPEG_INCLUDE_DIR})
  studio_remote_set_warnings(studio_remote_platform_windows)
  set(STUDIO_REMOTE_PLATFORM_TARGET studio_remote_platform_windows)
elseif(APPLE)
  enable_language(OBJCXX)
  set(STUDIO_REMOTE_MACOS_ICON_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/macos/icons")
  add_custom_command(
    OUTPUT "${STUDIO_REMOTE_MACOS_ICON_DIRECTORY}/panelive-companion.icns"
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/resources"
      "-DOUTPUT_DIRECTORY=${STUDIO_REMOTE_MACOS_ICON_DIRECTORY}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/resources/build_companion_icon.cmake"
    DEPENDS platform/macos/resources/build_companion_icon.cmake
      platform/windows/resources/panelive-companion-master.png
      platform/windows/resources/panelive-companion-16.png
      platform/windows/resources/panelive-companion-32.png
      platform/windows/resources/panelive-companion-64.png
      platform/windows/resources/panelive-companion-128.png
      platform/windows/resources/panelive-companion-256.png
    VERBATIM)
  add_custom_target(studio_remote_macos_icon ALL
    DEPENDS "${STUDIO_REMOTE_MACOS_ICON_DIRECTORY}/panelive-companion.icns")
  add_library(studio_remote_platform_macos STATIC
    platform/macos/platform_services/macos_host_diagnostics.cpp
    platform/macos/platform_services/macos_network_diagnostics.cpp
    platform/macos/capture/macos_video_capture_demand.cpp
    platform/macos/capture/macos_video_frames.cpp
    platform/macos/capture/macos_capture_worker.cpp
    platform/macos/capture/macos_thumbnail_capture.cpp
    platform/macos/capture/macos_thumbnail_encoder.cpp
    platform/macos/capture/macos_audio_mixer_roster.cpp
    platform/macos/capture/macos_audio_monitor_ipc.cpp
    platform/macos/capture/macos_audio_monitor.cpp
    platform/macos/capture/macos_audio_output_catalog.cpp
    platform/macos/capture/macos_audio_output_endpoint.cpp
    platform/macos/capture/macos_audio_diagnostic_player.cpp
    platform/macos/capture/macos_audio_diagnostic_render.cpp
    platform/macos/capture/macos_scene_item_properties.cpp
    platform/macos/capture/macos_source_media_inspector.mm
    platform/macos/ipc/read_only_shared_memory.cpp
    platform/macos/ipc/writable_shared_memory.cpp
    platform/macos/obs/macos_obs_runtime_capabilities.cpp
    platform/macos/obs/macos_obs_quick_transition_control.cpp
    platform/macos/obs/macos_obs_quick_transition_catalog.cpp
    platform/shared/obs/obs_saved_quick_transition_catalog.cpp
    platform/macos/obs/macos_obs_configuration_root.mm
    platform/macos/obs/macos_obs_connection.cpp
    platform/macos/obs/macos_obs_client.cpp
    platform/macos/obs/macos_obs_rpc_session.cpp
    platform/macos/obs/macos_obs_websocket.cpp
    platform/macos/obs/macos_obs_socket_connection.cpp
    platform/macos/discovery/macos_dns_sd.mm
    platform/macos/platform_services/macos_lan_address.mm
    platform/macos/platform_services/macos_listener_port.cpp
    platform/macos/platform_services/macos_realtime_audio_services.mm
    platform/macos/platform_services/macos_single_instance.cpp
    platform/macos/platform_services/macos_sign_in_startup.cpp
    platform/macos/platform_services/macos_sign_in_startup.mm
    platform/macos/platform_services/macos_shutdown_signal.cpp
    platform/macos/platform_services/macos_hotkey_sequence.cpp
    platform/macos/platform_services/macos_hotkey_executor.cpp
    platform/macos/platform_services/macos_config.mm
    platform/macos/platform_services/macos_config_file.cpp
    platform/macos/platform_services/macos_production_installations.mm
    platform/macos/credentials/macos_tls_identity.cpp
    platform/macos/credentials/macos_private_tls_storage.cpp
    platform/macos/credentials/macos_credential_store.mm
    platform/macos/credentials/macos_keychain_interaction.cpp
    platform/macos/credentials/macos_startup_password.cpp
    platform/macos/platform_services/macos_platform_services.mm)
  set_target_properties(studio_remote_platform_macos PROPERTIES
    OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
  target_compile_options(studio_remote_platform_macos PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_include_directories(studio_remote_platform_macos PUBLIC include)
  target_link_libraries(studio_remote_platform_macos PUBLIC
    studio_remote_audio_monitor_worker studio_remote_video_capture_worker
    studio_remote_core studio_remote_obs_adapter
    PRIVATE OpenSSL::SSL OpenSSL::Crypto LibDataChannel::LibDataChannel
    "-framework Foundation" "-framework Security" "-framework LocalAuthentication"
    "-framework SystemConfiguration" "-framework CoreGraphics" "-framework ImageIO"
    "-framework CoreAudio" "-framework AudioToolbox" "-framework ServiceManagement"
    "-framework AVFoundation" "-framework IOKit")
  studio_remote_set_warnings(studio_remote_platform_macos)
  if(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    target_sources(studio_remote_platform_macos PRIVATE
      platform/macos/platform_services/macos_streamlabs_installation.mm
      platform/macos/platform_services/macos_streamlabs_websocket_channel.cpp)
    target_link_libraries(studio_remote_platform_macos PUBLIC studio_remote_streamlabs_adapter)
  endif()
  set(STUDIO_REMOTE_PLATFORM_TARGET studio_remote_platform_macos)
  add_library(studio_remote_macos_installer_paths STATIC
    packaging/macos/installer/macos_obs_installation.mm)
  set_target_properties(studio_remote_macos_installer_paths PROPERTIES
    OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
  target_compile_options(studio_remote_macos_installer_paths PRIVATE -fobjc-arc)
  target_link_libraries(studio_remote_macos_installer_paths PRIVATE
    studio_remote_platform_macos "-framework AppKit")
  studio_remote_set_warnings(studio_remote_macos_installer_paths)
  add_library(studio_remote_macos_installer_record STATIC
    packaging/macos/installer/macos_installation_record.mm)
  set_target_properties(studio_remote_macos_installer_record PROPERTIES
    OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
  target_compile_options(studio_remote_macos_installer_record PRIVATE -fobjc-arc)
  target_link_libraries(studio_remote_macos_installer_record PRIVATE
    studio_remote_platform_macos "-framework Foundation")
  studio_remote_set_warnings(studio_remote_macos_installer_record)
  add_library(studio_remote_macos_management_bridge STATIC
    ui/macos/macos_client_notifications.mm
    ui/macos/macos_tray_icon.mm
    ui/macos/management_worker.mm
    ui/macos/management_pairing.mm
    ui/macos/management_pairing_preview.mm
    ui/macos/management_diagnostics.mm
    ui/macos/management_bridge.mm
    ui/macos/management_teleprompter.mm
    ui/macos/management_teleprompter_commands.mm
    ui/macos/management_presentation.mm)
  target_sources(studio_remote_macos_management_bridge PRIVATE ui/macos/management_branding.mm)
  set(STUDIO_REMOTE_MACOS_NAVIGATION_ARTWORK
    "${CMAKE_CURRENT_BINARY_DIR}/generated/macos/navigation_artwork.hpp")
  add_custom_command(OUTPUT "${STUDIO_REMOTE_MACOS_NAVIGATION_ARTWORK}"
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/ui/linux/icons"
      "-DOUTPUT_FILE=${STUDIO_REMOTE_MACOS_NAVIGATION_ARTWORK}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/resources/embed_navigation_artwork.cmake"
    DEPENDS platform/macos/resources/embed_navigation_artwork.cmake
      ui/linux/icons/dispatch-collapse-symbolic.svg ui/linux/icons/dispatch-expand-symbolic.svg
    VERBATIM)
  target_sources(studio_remote_macos_management_bridge PRIVATE
    ui/macos/management_navigation_images.mm "${STUDIO_REMOTE_MACOS_NAVIGATION_ARTWORK}")
  set(STUDIO_REMOTE_MACOS_TRAY_ARTWORK
    "${CMAKE_CURRENT_BINARY_DIR}/generated/macos/companion_tray_artwork.hpp")
  add_custom_command(OUTPUT "${STUDIO_REMOTE_MACOS_TRAY_ARTWORK}"
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/resources"
      "-DOUTPUT_FILE=${STUDIO_REMOTE_MACOS_TRAY_ARTWORK}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/resources/embed_companion_tray_artwork.cmake"
    DEPENDS platform/macos/resources/embed_companion_tray_artwork.cmake
      platform/windows/resources/panelive-companion-20.png
      platform/windows/resources/panelive-companion-40.png
      platform/windows/resources/panelive-companion-64.png
    VERBATIM)
  target_sources(studio_remote_macos_management_bridge PRIVATE "${STUDIO_REMOTE_MACOS_TRAY_ARTWORK}")
  target_include_directories(studio_remote_macos_management_bridge PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/macos")
  set_target_properties(studio_remote_macos_management_bridge PROPERTIES
    OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
  target_compile_options(studio_remote_macos_management_bridge PRIVATE -fobjc-arc)
  target_link_libraries(studio_remote_macos_management_bridge PUBLIC studio_remote_application
    PRIVATE studio_remote_pairing_qr "-framework Foundation" "-framework AppKit"
      "-framework UserNotifications")
  # Load the named-command Objective-C category from the static bridge archive.
  target_link_options(studio_remote_macos_management_bridge INTERFACE "LINKER:-ObjC")
  studio_remote_set_warnings(studio_remote_macos_management_bridge)
  enable_language(Swift)
  if(CMAKE_Swift_COMPILER_ID STREQUAL "Apple")
    # Supply libc++ once for the native C++ bridge. CMake 3.31 otherwise appends it separately
    # for C++ and Objective-C++ dependencies of a Swift executable.
    string(APPEND CMAKE_Swift_STANDARD_LIBRARIES " -lc++")
    list(APPEND CMAKE_Swift_IMPLICIT_LINK_LIBRARIES c++)
  endif()
  add_library(studio_remote_ui_macos STATIC
    ui/macos/ManagementTeleprompterPage.swift
    ui/macos/ManagementTeleprompterSharing.swift
    ui/macos/ManagementPagesHost.swift
    ui/macos/ManagementFormChoices.swift
    ui/macos/ManagementAboutPage.swift
    ui/macos/ManagementAdvancedPage.swift
    ui/macos/ManagementDiagnosticsPage.swift
    ui/macos/ManagementShellModel.swift
    ui/macos/ManagementShell.swift
    ui/macos/ManagementApprovalPrompt.swift
    ui/macos/ManagementReportExport.swift
    ui/macos/ManagementWindow.swift
    ui/macos/ManagementPairingCard.swift
    ui/macos/ManagementPairingOverlay.swift
    ui/macos/ManagementAdaptersPage.swift
    ui/macos/ManagementNetworkPage.swift
    ui/macos/ManagementPageHeading.swift
    ui/macos/ManagementClientsPage.swift
    ui/macos/ManagementClientsRoster.swift
    ui/macos/ManagementClientDetails.swift
    ui/macos/ManagementStatusPage.swift)
  set_target_properties(studio_remote_ui_macos PROPERTIES Swift_LANGUAGE_VERSION 6)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/macos")
  set(STUDIO_REMOTE_MACOS_SWIFT_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/macos/ManagementWindow-Swift.h")
  # Ninja must finish Swift emission before compiling any Objective-C++ consumer, including
  # when an older generated header is already present in an incremental build.
  add_custom_command(TARGET studio_remote_ui_macos POST_BUILD
    BYPRODUCTS "${STUDIO_REMOTE_MACOS_SWIFT_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E true
    VERBATIM)
  set_source_files_properties(ui/macos/macos_management_ui.mm
    ui/macos/management_approvals.mm PROPERTIES
    OBJECT_DEPENDS "${STUDIO_REMOTE_MACOS_SWIFT_HEADER}")
  target_compile_options(studio_remote_ui_macos PRIVATE
    -target "${STUDIO_REMOTE_MACOS_SWIFT_TARGET}"
    -import-objc-header "${CMAKE_CURRENT_SOURCE_DIR}/ui/macos/ManagementBridge.h"
    -emit-objc-header -emit-objc-header-path "${STUDIO_REMOTE_MACOS_SWIFT_HEADER}"
    $<$<BOOL:${STUDIO_REMOTE_WARNINGS_AS_ERRORS}>:-warnings-as-errors>)
  target_link_libraries(studio_remote_ui_macos PRIVATE
    "$<LINK_ONLY:studio_remote_macos_management_bridge>"
    "-framework SwiftUI" "-framework AppKit")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig REQUIRED)
  find_package(JPEG REQUIRED)
  if(STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "ARCH")
    set(STUDIO_REMOTE_JPEG_LINK_TARGET JPEG::JPEG)
    find_file(STUDIO_REMOTE_JPEG_NOTICE LICENSE.md
      PATHS /usr/share/licenses/libjpeg-turbo NO_DEFAULT_PATH REQUIRED)
  else()
    # Ubuntu and Debian use different libjpeg SONAMEs. Their common binary uses the archive.
    find_library(STUDIO_REMOTE_JPEG_ARCHIVE NAMES libjpeg.a REQUIRED)
    set(STUDIO_REMOTE_JPEG_LINK_TARGET "${STUDIO_REMOTE_JPEG_ARCHIVE}")
    find_file(STUDIO_REMOTE_JPEG_NOTICE copyright
      PATHS /usr/share/doc/libjpeg-turbo8-dev /usr/share/doc/libjpeg62-turbo-dev
      NO_DEFAULT_PATH REQUIRED)
  endif()
  pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1>=0.20)
  pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client>=0.8)
  pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse>=15.0)
  pkg_check_modules(PULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple>=15.0)
  add_library(studio_remote_platform_linux STATIC
    platform/linux/capture/linux_audio_diagnostic_player.cpp
    platform/linux/capture/linux_audio_mixer_roster.cpp
    platform/linux/capture/linux_audio_monitor.cpp
    platform/linux/capture/linux_audio_output_catalog.cpp
    platform/linux/capture/linux_capture.cpp
    platform/linux/capture/linux_scene_item_properties.cpp
    platform/linux/capture/linux_thumbnail_capture.cpp
    platform/linux/capture/linux_thumbnail_encoder.cpp
    platform/linux/credentials/linux_credential_store.cpp
    platform/linux/credentials/linux_crypto.cpp
    platform/linux/credentials/linux_tls_identity.cpp
    platform/linux/credentials/linux_private_tls_storage.cpp
    platform/linux/ipc/linux_ipc_mapping.cpp
    platform/linux/capture/linux_video_frames.cpp
    platform/linux/capture/linux_video_capture_demand.cpp
    platform/linux/capture/linux_audio_monitor_ipc.cpp
    platform/linux/capture/linux_thumbnail_mailbox.cpp
    platform/linux/platform_services/linux_config.cpp
    platform/linux/platform_services/linux_autostart.cpp
    platform/linux/platform_services/linux_network.cpp
    platform/linux/platform_services/linux_obs_hotkey_catalog.cpp
    platform/linux/platform_services/linux_obs_client.cpp
    platform/linux/platform_services/linux_obs_quick_transition_catalog.cpp
    platform/linux/platform_services/linux_obs_quick_transition_control.cpp
    platform/linux/platform_services/linux_obs_runtime_capabilities.cpp
    platform/linux/platform_services/linux_platform_services.cpp
    platform/linux/platform_services/linux_process.cpp)
  target_include_directories(studio_remote_platform_linux PUBLIC include)
  target_link_libraries(studio_remote_platform_linux PUBLIC
    studio_remote_audio_monitor_worker studio_remote_video_capture_worker
    studio_remote_core
    studio_remote_obs_adapter
    LibDataChannel::LibDataChannel
    ${STUDIO_REMOTE_JPEG_LINK_TARGET}
    OpenSSL::Crypto
    PkgConfig::AVAHI
    PkgConfig::LIBSECRET
    PkgConfig::PULSE
    PkgConfig::PULSE_SIMPLE)
  studio_remote_set_warnings(studio_remote_platform_linux)
  target_include_directories(studio_remote_platform_linux PRIVATE "${JPEG_INCLUDE_DIR}")
  set(STUDIO_REMOTE_PLATFORM_TARGET studio_remote_platform_linux)
else()
  message(FATAL_ERROR "Unsupported platform")
endif()

if(WIN32)
  add_library(studio_remote_ui_windows STATIC
    ui/windows/windows_tray_icon.cpp
    ui/windows/windows_management_ui.cpp
    ui/windows/winui_about_page.cpp
    ui/windows/winui_advanced_settings_page.cpp
    ui/windows/winui_clients_page.cpp
    ui/windows/winui_teleprompter_page.cpp
    ui/windows/winui_teleprompter_sharing.cpp
    ui/windows/winui_client_identification.cpp
    ui/windows/winui_pairing_preview.cpp
    ui/windows/winui_connections_pages.cpp
    ui/windows/winui_diagnostics_page.cpp
    ui/windows/winui_form_choices.cpp
    ui/windows/winui_management_pages_host.cpp
    ui/windows/winui_management_window.cpp
    ui/windows/winui_management_approvals.cpp
    ui/windows/winui_page_helpers.cpp
    ui/windows/winui_status_page.cpp)
  target_include_directories(studio_remote_ui_windows PUBLIC ui/windows PRIVATE
    ${QRENCODE_INCLUDE_DIR})
  target_compile_definitions(studio_remote_ui_windows PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
  target_link_libraries(studio_remote_ui_windows PUBLIC studio_remote_platform_windows PRIVATE
    studio_remote_application studio_remote_pairing_qr WindowsAppSDK::WinUI ${QRENCODE_LIBRARY} comctl32 comdlg32 dwmapi
    gdi32 shell32 uxtheme windowscodecs)
  studio_remote_set_warnings(studio_remote_ui_windows)
  set(STUDIO_REMOTE_UI_TARGET studio_remote_ui_windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4>=4.6)
  pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1>=1.1)
  find_program(GLIB_COMPILE_RESOURCES_EXECUTABLE glib-compile-resources REQUIRED)
  set(STUDIO_REMOTE_LINUX_UI_RESOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/linux_management_resources.c")
  # GLib emits one resource byte string, which can exceed C99's minimum supported length.
  # GCC and Clang support it; keep the exception scoped to generated resource data only.
  set_source_files_properties("${STUDIO_REMOTE_LINUX_UI_RESOURCES}" PROPERTIES
    COMPILE_OPTIONS "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wno-overlength-strings>")
  add_custom_command(
    OUTPUT "${STUDIO_REMOTE_LINUX_UI_RESOURCES}"
    COMMAND "${GLIB_COMPILE_RESOURCES_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/ui/linux/linux_management_resources.xml"
      --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}
      --generate-source
      --manual-register
      --dependency-file=${STUDIO_REMOTE_LINUX_UI_RESOURCES}.d
      --target=${STUDIO_REMOTE_LINUX_UI_RESOURCES}
    DEPFILE "${STUDIO_REMOTE_LINUX_UI_RESOURCES}.d"
    DEPENDS
      ui/linux/linux_management_resources.xml
      platform/windows/resources/panelive-companion-32.png
      platform/windows/resources/panelive-companion-128.png
    VERBATIM)
  add_library(studio_remote_ui_linux STATIC
    "${STUDIO_REMOTE_LINUX_UI_RESOURCES}"
    ui/linux/linux_appearance.cpp
    ui/linux/omarchy_palette.cpp
    ui/linux/linux_about_page.cpp
    ui/linux/linux_approval_dialogs.cpp
    ui/linux/linux_advanced_settings_page.cpp
    ui/linux/linux_clients_page.cpp
    ui/linux/linux_clients_toolbar.cpp
    ui/linux/linux_teleprompter_page.cpp
    ui/linux/linux_teleprompter_editor.cpp
    ui/linux/linux_teleprompter_sharing.cpp
    ui/linux/linux_client_identification.cpp
    ui/linux/linux_connections_pages.cpp
    ui/linux/linux_diagnostics_page.cpp
    ui/linux/linux_management_ui.cpp
    ui/linux/linux_management_window.cpp
    ui/linux/linux_management_window_layout.cpp
    ui/linux/linux_management_pages_host.cpp
    ui/linux/linux_form_choices.cpp
    ui/linux/linux_pairing_qr.cpp
    ui/linux/linux_pairing_preview.cpp
    ui/linux/linux_status_page.cpp
    ui/linux/linux_status_notifier_item.cpp
    ui/linux/linux_status_notifier_updates.cpp
    ui/linux/linux_desktop_status.cpp
    ui/linux/linux_notification_delivery.cpp
    ui/linux/linux_ui_helpers.cpp)
  target_include_directories(studio_remote_ui_linux PUBLIC ui/linux)
  target_compile_definitions(studio_remote_ui_linux PUBLIC
    GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_72
    GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_72
    GDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_6
    GDK_VERSION_MAX_ALLOWED=GDK_VERSION_4_6
    ADW_VERSION_MIN_REQUIRED=ADW_VERSION_1_1
    ADW_VERSION_MAX_ALLOWED=ADW_VERSION_1_1)
  target_link_libraries(studio_remote_ui_linux PUBLIC
    studio_remote_pairing_qr
    studio_remote_platform_linux
    studio_remote_application
    PkgConfig::ADWAITA
    PkgConfig::GTK4)
  studio_remote_set_warnings(studio_remote_ui_linux)
  set(STUDIO_REMOTE_UI_TARGET studio_remote_ui_linux)
endif()

if(WIN32)
  set(STUDIO_REMOTE_COMPANION_APPLICATION_SOURCE
    app/gateway_main/windows_startup_options.cpp
    app/gateway_main/windows_companion_application.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(STUDIO_REMOTE_COMPANION_APPLICATION_SOURCE
    app/gateway_main/linux_client_approvals.cpp
    app/gateway_main/linux_command_line.cpp
    app/gateway_main/linux_companion_application.cpp
    app/gateway_main/linux_gateway_advertisement.cpp
    app/gateway_main/linux_management_composition.cpp)
elseif(APPLE)
  set(STUDIO_REMOTE_COMPANION_APPLICATION_SOURCE
    app/gateway_main/macos_application_services.cpp
    app/gateway_main/macos_companion_application.mm
    app/gateway_main/macos_command_line.cpp
    app/gateway_main/macos_installer_operations.cpp
    app/gateway_main/macos_gateway_composition.cpp
    app/gateway_main/macos_management_composition.cpp)
  list(APPEND STUDIO_REMOTE_COMPANION_APPLICATION_SOURCE
    ui/macos/macos_management_ui.mm ui/macos/management_approvals.mm
    ui/macos/macos_startup_authorization.mm)
else()
  set(STUDIO_REMOTE_COMPANION_APPLICATION_SOURCE
    app/gateway_main/unavailable_companion_application.cpp)
endif()

add_executable(panelive-companion
  app/gateway_main/client_event_protocol_registry.cpp
  app/gateway_main/countdown_event_protocol.cpp
  app/gateway_main/http_client_events.cpp
  app/gateway_main/http_websocket_messages.cpp
  app/gateway_main/http_authentication.cpp
  app/gateway_main/client_approval_callbacks.cpp
  app/gateway_main/adapter_gateway_controller.cpp
  app/gateway_main/listener_gateway_controller.cpp
  app/gateway_main/obs_media_delivery.cpp
  app/gateway_main/vmix_video_health.cpp
  app/gateway_main/hotkey_protocol.cpp
  app/gateway_main/main.cpp
  app/gateway_main/media_session_reset_protocol.cpp
  app/gateway_main/media_session_reset_registry.cpp
  app/gateway_main/http_server.cpp
  app/gateway_main/http_tally_publications.cpp
  app/gateway_main/http_state_delivery.cpp
  app/gateway_main/http_request_policy.cpp
  app/gateway_main/http_callback_lifetime.cpp
  app/gateway_main/http_request_cleanup.cpp
  app/gateway_main/teleprompter_protocol.cpp
  app/gateway_main/teleprompter_session_protocol.cpp
  app/gateway_main/telestrator_protocol.cpp
  app/gateway_main/production_catalog_protocol.cpp
  app/gateway_main/http_role_requests.cpp
  app/gateway_main/http_tls_context.cpp
  app/gateway_main/obs_configuration_protocol.cpp
  app/gateway_main/pairing_exchange_protocol.cpp
  app/gateway_main/webrtc_recovery_coordinator.cpp
  app/gateway_main/webrtc_recovery_protocol.cpp
  app/gateway_main/websocket_frame_router.cpp
  ${STUDIO_REMOTE_COMPANION_APPLICATION_SOURCE})
target_include_directories(panelive-companion PRIVATE include core/application
  "${CMAKE_CURRENT_BINARY_DIR}/generated/build_config")
target_link_libraries(panelive-companion PRIVATE
  studio_remote_teleprompter_adapter
  studio_remote_teleprompter_storage
  studio_remote_tally_storage
  studio_remote_tally_routes
  studio_remote_application
  studio_remote_transport
  ${STUDIO_REMOTE_PLATFORM_TARGET}
  ${STUDIO_REMOTE_UI_TARGET})
target_link_libraries(panelive-companion PRIVATE civetweb::civetweb-cpp OpenSSL::SSL)
if(APPLE)
  target_link_libraries(panelive-companion PRIVATE
    studio_remote_macos_installer_paths studio_remote_macos_installer_record)
endif()
if(APPLE)
  target_link_libraries(panelive-companion PRIVATE
    studio_remote_ui_macos studio_remote_macos_management_bridge "-framework AppKit")
  add_dependencies(panelive-companion studio_remote_ui_macos)
  target_include_directories(panelive-companion PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/macos")
  target_compile_options(panelive-companion PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  set_target_properties(panelive-companion PROPERTIES
    OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF
    LINKER_LANGUAGE CXX)
  execute_process(COMMAND "${CMAKE_Swift_COMPILER}" -print-target-info
    -target "${STUDIO_REMOTE_MACOS_SWIFT_TARGET}"
    OUTPUT_VARIABLE STUDIO_REMOTE_SWIFT_TARGET_INFO COMMAND_ERROR_IS_FATAL ANY)
  string(JSON STUDIO_REMOTE_SWIFT_RUNTIME_DIRECTORY GET
    "${STUDIO_REMOTE_SWIFT_TARGET_INFO}" paths runtimeResourcePath)
  target_link_directories(panelive-companion PRIVATE
    "${STUDIO_REMOTE_SWIFT_RUNTIME_DIRECTORY}/macosx" "${CMAKE_OSX_SYSROOT}/usr/lib/swift")
endif()

target_compile_definitions(panelive-companion PRIVATE
  PROJECT_VERSION="${STUDIO_REMOTE_BUILD_VERSION}"
  PROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  PROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  PROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH})
if(WIN32)
  enable_language(RC)
  target_sources(panelive-companion PRIVATE
    platform/windows/resources/panelive-companion.rc)
  set_source_files_properties(platform/windows/resources/panelive-companion.rc
    PROPERTIES COMPILE_DEFINITIONS "STUDIO_REMOTE_IS_PRERELEASE=${STUDIO_REMOTE_IS_PRERELEASE}")
  target_compile_definitions(panelive-companion PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  if(STUDIO_REMOTE_VMIX_AUDIO_ADAPTER STREQUAL "OMT")
    add_custom_command(TARGET panelive-companion POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${STUDIO_REMOTE_OMT_RUNTIME}" "$<TARGET_FILE_DIR:panelive-companion>")
  endif()
endif()
studio_remote_set_warnings(panelive-companion)
set_target_properties(panelive-companion PROPERTIES
  OUTPUT_NAME "${PANELIVE_COMPANION_PACKAGE_ID}")
if(APPLE)
  include(cmake/platform/MacosBundle.cmake)
  studio_remote_configure_macos_bundle(panelive-companion)
  include(cmake/platform/MacosInstaller.cmake)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(panelive-companion PRIVATE
    $<$<CONFIG:Release>:-Wl,--strip-all>)
endif()
if(WIN32)
  set_target_properties(panelive-companion PROPERTIES WIN32_EXECUTABLE TRUE)
  if(MSVC)
    target_link_options(panelive-companion PRIVATE /ENTRY:mainCRTStartup)
  endif()
  add_custom_command(TARGET panelive-companion POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${STUDIO_REMOTE_WINDOWS_APP_SDK_BOOTSTRAP_DLL}"
      "$<TARGET_FILE_DIR:panelive-companion>")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(obs-plugin)
endif()

install(TARGETS
  panelive-companion
  RUNTIME DESTINATION bin
  BUNDLE DESTINATION .)
if(WIN32)
  install(FILES "${STUDIO_REMOTE_WINDOWS_APP_SDK_BOOTSTRAP_DLL}" DESTINATION bin)
  if(STUDIO_REMOTE_STAGE_WINDOWS_APP_RUNTIME_INSTALLER)
    install(FILES "${STUDIO_REMOTE_WINDOWS_APP_RUNTIME_INSTALLER}" DESTINATION redist)
  endif()
  install(FILES "${STUDIO_REMOTE_WINDOWS_APP_SDK_LICENSE}"
    DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/licenses/windows-app-sdk"
    RENAME copyright)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  install(FILES "${STUDIO_REMOTE_JPEG_NOTICE}"
    DESTINATION "share/panelive-companion/licenses"
    RENAME libjpeg-turbo.txt)
  install(FILES
    platform/linux/packaging/com.clownfishweb.PaneLiveCompanion.desktop
    DESTINATION share/applications)
  install(FILES
    platform/linux/packaging/com.clownfishweb.PaneLiveCompanion.metainfo.xml
    DESTINATION share/metainfo)
  foreach(STUDIO_REMOTE_ICON_SIZE 16 20 24 32 40 48 64 128 256)
    install(FILES
      "platform/windows/resources/panelive-companion-${STUDIO_REMOTE_ICON_SIZE}.png"
      DESTINATION "share/icons/hicolor/${STUDIO_REMOTE_ICON_SIZE}x${STUDIO_REMOTE_ICON_SIZE}/apps"
      RENAME com.clownfishweb.PaneLiveCompanion.png)
  endforeach()
  install(FILES platform/windows/resources/panelive-companion-master.png
    DESTINATION share/icons/hicolor/1024x1024/apps
    RENAME com.clownfishweb.PaneLiveCompanion.png)
  install(FILES
    platform/linux/packaging/panelive-companion-autostart.desktop
    DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/autostart")
  if(STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "ARCH")
    install(DIRECTORY ui/linux/omarchy-widget/
      DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/omarchy-widget")
  endif()
  find_program(GZIP_EXECUTABLE gzip REQUIRED)
  if(STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "DEBIAN")
    configure_file(platform/linux/packaging/changelog
      "${CMAKE_CURRENT_BINARY_DIR}/changelog" COPYONLY)
    execute_process(
      COMMAND "${GZIP_EXECUTABLE}" -9 -n -c "${CMAKE_CURRENT_BINARY_DIR}/changelog"
      OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz"
      COMMAND_ERROR_IS_FATAL ANY)
    install(FILES platform/linux/packaging/debian-copyright
      DESTINATION "share/doc/${PANELIVE_COMPANION_PACKAGE_ID}"
      RENAME copyright)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz"
      DESTINATION "share/doc/${PANELIVE_COMPANION_PACKAGE_ID}")
    install(FILES platform/linux/packaging/obs-plugin-copyright
      DESTINATION "share/doc/${PANELIVE_COMPANION_PACKAGE_ID}"
      RENAME obs-plugin-copyright COMPONENT OBSPlugin)
  endif()
  configure_file(platform/linux/packaging/panelive-companion.1
    "${CMAKE_CURRENT_BINARY_DIR}/panelive-companion.1" COPYONLY)
  execute_process(
    COMMAND "${GZIP_EXECUTABLE}" -9 -n -c
      "${CMAKE_CURRENT_BINARY_DIR}/panelive-companion.1"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/panelive-companion.1.gz"
    COMMAND_ERROR_IS_FATAL ANY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/panelive-companion.1.gz"
    DESTINATION share/man/man1)
  if(STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "DEBIAN")
    install(FILES platform/linux/packaging/lintian-overrides
      DESTINATION "share/lintian/overrides"
      RENAME "${PANELIVE_COMPANION_PACKAGE_ID}")
  endif()
endif()
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
  set(STUDIO_REMOTE_WINDOWS_RUNTIME_DIRECTORY
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  set(STUDIO_REMOTE_WINDOWS_RUNTIME_DLL_NAMES
    avcodec-63.dll
    avformat-63.dll
    avutil-61.dll
    civetweb-cpp.dll
    civetweb.dll
    datachannel.dll
    juice.dll
    libcrypto-3-x64.dll
    libssl-3-x64.dll
    opus.dll
    qrencode.dll
    sqlite3.dll
    srt.dll
    srtp2.dll
    swscale-10.dll
    z.dll)
  set(STUDIO_REMOTE_WINDOWS_RUNTIME_DLLS)
  foreach(runtime_name IN LISTS STUDIO_REMOTE_WINDOWS_RUNTIME_DLL_NAMES)
    set(runtime_path "${STUDIO_REMOTE_WINDOWS_RUNTIME_DIRECTORY}/${runtime_name}")
    if(NOT EXISTS "${runtime_path}")
      message(FATAL_ERROR "Missing whitelisted Windows runtime DLL: ${runtime_path}")
    endif()
    list(APPEND STUDIO_REMOTE_WINDOWS_RUNTIME_DLLS "${runtime_path}")
  endforeach()
  install(FILES ${STUDIO_REMOTE_WINDOWS_RUNTIME_DLLS} DESTINATION bin)
else()
  install(FILES $<TARGET_RUNTIME_DLLS:panelive-companion> DESTINATION bin)
endif()

if(WIN32 AND STUDIO_REMOTE_VMIX_AUDIO_ADAPTER STREQUAL "OMT")
  install(FILES "${STUDIO_REMOTE_OMT_RUNTIME}" DESTINATION bin)
  install(FILES "${STUDIO_REMOTE_OMT_LICENSE}"
    DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/licenses/omt"
    RENAME copyright)
endif()
install(FILES LICENSE NOTICE THIRD_PARTY_NOTICES.md
  DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}")
if(WIN32)
  install(FILES
    packaging/windows/RUN_CURRENT_MACHINE.cmd
    DESTINATION .)
elseif(APPLE)
  install(PROGRAMS packaging/macos/Install-ObsPlugin.sh packaging/macos/Install-ObsPlugin.command
    DESTINATION .)
  install(FILES packaging/macos/INSTALL_OBS_PLUGIN_MACOS.md DESTINATION .)
endif()
install(FILES README.md
  DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}")
install(DIRECTORY docs/ DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/docs")
install(DIRECTORY releases/ DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/releases"
  PATTERN "SHA256SUMS*" EXCLUDE)
install(FILES obs-plugin/LICENSE
  DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/obs-plugin")
install(FILES packaging/macos/INSTALL_OBS_PLUGIN_MACOS.md
  DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/packaging/macos")
install(DIRECTORY generated/protocol/
  DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/protocol")
if(DEFINED VCPKG_INSTALLED_DIR)
  set(STUDIO_REMOTE_SHIPPED_LICENSES
    civetweb
    ffmpeg
    libdatachannel
    libjuice
    libqrencode
    libvpx
    libsrtp
    nlohmann-json
    openssl
    opus
    plog
    sqlite3
    usrsctp
    zlib)
  if(WIN32 AND STUDIO_REMOTE_ENABLE_PROVIDER_VMIX)
    list(APPEND STUDIO_REMOTE_SHIPPED_LICENSES libsrt)
  endif()
  foreach(dependency IN LISTS STUDIO_REMOTE_SHIPPED_LICENSES)
    set(copyright_file
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${dependency}/copyright")
    if(EXISTS "${copyright_file}")
      install(FILES "${copyright_file}"
        DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/licenses/${dependency}")
      if(APPLE)
        studio_remote_add_macos_bundle_resource(panelive-companion
          "${copyright_file}"
          "share/${PANELIVE_COMPANION_PACKAGE_ID}/licenses/${dependency}")
      endif()
    else()
      message(FATAL_ERROR "Missing required license text: ${copyright_file}")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      set(sbom_file
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${dependency}/vcpkg.spdx.json")
      if(NOT EXISTS "${sbom_file}")
        message(FATAL_ERROR "Missing dependency SPDX inventory: ${sbom_file}")
      endif()
      install(FILES "${sbom_file}"
        DESTINATION "share/${PANELIVE_COMPANION_PACKAGE_ID}/sbom"
        RENAME "${dependency}.spdx.json")
    endif()
  endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   STUDIO_REMOTE_LINUX_DISTRIBUTION STREQUAL "DEBIAN")
  set(CPACK_GENERATOR DEB)
  set(CPACK_PACKAGE_NAME "${PANELIVE_COMPANION_PACKAGE_ID}")
  set(CPACK_DEBIAN_PACKAGE_NAME "${PANELIVE_COMPANION_PACKAGE_ID}")
  set(CPACK_PACKAGE_VENDOR "ClownfishWeb")
  set(CPACK_PACKAGE_CONTACT "ClownfishWeb <support@clownfishweb.com>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Secure mobile production control and monitoring")
  set(CPACK_PACKAGE_DESCRIPTION "PaneLive Companion connects paired mobile clients to production software.\n This Linux package supports OBS Studio and a standalone teleprompter.\n The native GTK management window controls pairing and local approvals.\n Includes the matching OBS helper, activated automatically for compatible\n native OBS installations.")
  set(CPACK_PACKAGE_VERSION "${STUDIO_REMOTE_BUILD_VERSION}")
  set(CPACK_DEBIAN_PACKAGE_VERSION "${STUDIO_REMOTE_DEBIAN_VERSION}")
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
  set(CPACK_DEBIAN_PACKAGE_SECTION video)
  set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
  set(CPACK_DEBIAN_FILE_NAME "panelive-companion-${STUDIO_REMOTE_BUILD_VERSION}-linux-x64.deb")
  set(CPACK_DEB_COMPONENT_INSTALL OFF)
  # Scan the two binaries independently at packaging time: Companion's libraries
  # are required; OBS integration libraries are optional until helper activation.
  # The installer verifies all helper relocations before exposing it to OBS.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
  configure_file(platform/linux/packaging/dependencies.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/linux-deb-dependencies.cmake" @ONLY)
  set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/linux-deb-dependencies.cmake")
  set(CPACK_DEBIAN_PACKAGE_CONFLICTS "panelive-companion-obs-plugin")
  set(CPACK_DEBIAN_PACKAGE_REPLACES "panelive-companion-obs-plugin")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS
    "gnome-keyring, dbus-user-session, avahi-daemon")
  set(STUDIO_REMOTE_LINUX_HELPER_CONTROL_DIR "${CMAKE_CURRENT_BINARY_DIR}/linux-deb-control")
  file(MAKE_DIRECTORY "${STUDIO_REMOTE_LINUX_HELPER_CONTROL_DIR}")
  foreach(control_file postinst prerm postrm triggers)
    configure_file("platform/linux/packaging/${control_file}.in"
      "${STUDIO_REMOTE_LINUX_HELPER_CONTROL_DIR}/${control_file}" @ONLY)
    list(APPEND CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
      "${STUDIO_REMOTE_LINUX_HELPER_CONTROL_DIR}/${control_file}")
  endforeach()
  configure_file(platform/linux/packaging/configure-obs-helper.in
    "${CMAKE_CURRENT_BINARY_DIR}/configure-obs-helper" @ONLY)
  install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/configure-obs-helper"
    DESTINATION lib/panelive-companion)
  configure_file(platform/linux/packaging/checksums.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/linux-deb-checksums.cmake" @ONLY)
  set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_CURRENT_BINARY_DIR}/linux-deb-checksums.cmake")
  set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
  set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/out/package/linux")
  set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
  include(CPack)
endif()
