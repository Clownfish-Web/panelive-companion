#include "../../core/application/tally_publication_service.hpp"
#include "../../platform/shared/tally/sqlite_publication_repository.hpp"
#include "../../platform/shared/teleprompter/session_device_source.hpp"
#include "../../platform/windows/platform_services/windows_tally_health_probe.hpp"
#include "adapter_gateway_controller.hpp"
#include "client_approval_callbacks.hpp"
#include "tally_management_callbacks.hpp"
#include "device_role_protocol.hpp"
#include "http_server.hpp"
#include "listener_gateway_controller.hpp"
#include "obs_media_delivery.hpp"
#include "platform_companion_application.hpp"
#include "vmix_video_health.hpp"
#include "windows_startup_options.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <studio_remote/build/remote_tally_service.hpp>
#include <studio_remote/configuration/stored_password.hpp>
#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/credentials/pairing_manager.hpp>
#include <studio_remote/discovery/companion_service_record.hpp>
#include <studio_remote/discovery/lan_address_selection.hpp>
#include <studio_remote/observability/endurance_event_log.hpp>
#include <studio_remote/observability/endurance_logging_setting.hpp>
#include <studio_remote/observability/endurance_process_log.hpp>
#include <studio_remote/observability/endurance_snapshot_registration.hpp>
#include <studio_remote/observability/obs_endurance_snapshot.hpp>
#include <studio_remote/provider/provider_catalog.hpp>
#include <studio_remote/provider/provider_selection.hpp>
#include <studio_remote/teleprompter/adapter.hpp>
#include <studio_remote/ui/management_adapter_rows.hpp>
#include <studio_remote/ui/management_clients.hpp>
#include <studio_remote/vmix/vmix_adapter.hpp>

#ifdef _WIN32
#include "../../platform/windows/capture/vmix_audio_stream_supervisor.hpp"
#include "../../platform/windows/capture/vmix_video_stream_supervisor.hpp"
#include "../../platform/windows/capture/windows_audio_diagnostic_player.hpp"
#include "../../platform/windows/capture/windows_audio_mixer_roster.hpp"
#include "../../platform/windows/capture/windows_audio_monitor.hpp"
#include "../../platform/windows/capture/windows_capture.hpp"
#include "../../platform/windows/capture/windows_scene_item_properties.hpp"
#include "../../platform/windows/capture/windows_source_media_inspector.hpp"
#include "../../platform/windows/capture/windows_thumbnail_capture.hpp"
#include "../../platform/windows/capture/windows_thumbnail_encoder.hpp"
#include "../../platform/windows/capture/windows_vmix_input_category_source.hpp"
#include "../../platform/windows/capture/windows_vmix_thumbnail_capture.hpp"
#if defined(STUDIO_REMOTE_VMIX_VIDEO_ADAPTER_SRT)
#include "../../platform/windows/capture/windows_vmix_srt_capture.hpp"
#endif
#if defined(STUDIO_REMOTE_HAS_NDI)
#include "../../platform/windows/capture/windows_ndi_audio_stream_adapter.hpp"
#include "../../platform/windows/capture/windows_ndi_capture.hpp"
#include "../../platform/windows/capture/windows_ndi_runtime.hpp"
#include "../../platform/windows/capture/windows_vmix_output_geometry_source.hpp"
#endif
#if defined(STUDIO_REMOTE_VMIX_AUDIO_ADAPTER_OMT)
#include "../../platform/windows/capture/windows_omt_audio_stream_adapter.hpp"
#include "../../platform/windows/capture/windows_omt_runtime.hpp"
#endif
#include "../../platform/shared/teleprompter/sqlite_teleprompter_repository.hpp"
#include "../../platform/windows/credentials/windows_credential_store.hpp"
#include "../../platform/windows/platform_services/windows_config.hpp"
#include "../../platform/windows/platform_services/windows_dns_sd.hpp"
#include "../../platform/windows/platform_services/windows_host_diagnostics.hpp"
#include "../../platform/windows/platform_services/windows_hotkey_executor.hpp"
#include "../../platform/windows/platform_services/windows_lan_address.hpp"
#include "../../platform/windows/platform_services/windows_listener_port.hpp"
#include "../../platform/windows/platform_services/windows_port_diagnostics.hpp"
#include "../../platform/windows/platform_services/windows_production_installations.hpp"
#include "../../platform/windows/platform_services/windows_realtime_audio_services.hpp"
#include "../../platform/windows/platform_services/windows_shutdown_signal.hpp"
#include "../../platform/windows/platform_services/windows_sign_in_startup.hpp"
#include "../../platform/windows/platform_services/windows_single_instance.hpp"
#include "../../platform/windows/platform_services/windows_vmix_streaming_destination_source.hpp"
#include "../../platform/windows/platform_services/windows_vmix_telestrator_probe.hpp"
#include "../../platform/windows/platform_services/winhttp_obs_client.hpp"
#include "../../platform/windows/platform_services/winhttp_vmix_transport.hpp"
#include "../../ui/windows/windows_management_ui.hpp"
#include <studio_remote/ui/management_status_snapshot_builder.hpp>
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
#include "../../platform/windows/platform_services/windows_streamlabs_installations.hpp"
#include "../../platform/windows/platform_services/windows_streamlabs_named_pipe_transport.hpp"
#include <studio_remote/streamlabs/streamlabs_adapter.hpp>
#endif
#include <conio.h>
// Windows networking headers must precede Windows.h.
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>
// clang-format on
#include <openssl/applink.c>
#endif

namespace
{
#ifdef _WIN32
std::string ascii_lower(std::string value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::string pairing_gateway_url(std::string_view address, std::uint16_t port)
{
  if (address.empty())
    throw std::runtime_error("No active private IPv4 LAN address is available for mobile pairing");
  return "https://" + std::string(address) + ":" + std::to_string(port);
}

bool prepare_interactive_input()
{
  if (GetConsoleWindow() != nullptr)
  {
    DWORD process_ids[2]{};
    const auto process_count = GetConsoleProcessList(process_ids, 2);
    if (process_count == 1)
      FreeConsole();
  }

  const bool attached_to_existing_console =
      GetConsoleWindow() != nullptr || AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
  if (attached_to_existing_console)
  {
    FILE* stream = nullptr;
    static_cast<void>(freopen_s(&stream, "CONIN$", "r", stdin));
    static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stdout));
    static_cast<void>(freopen_s(&stream, "CONOUT$", "w", stderr));
    std::cin.clear();
    std::cout.clear();
    std::cerr.clear();
  }

  const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (input == nullptr || input == INVALID_HANDLE_VALUE)
    return false;

  DWORD mode = 0;
  if (GetConsoleMode(input, &mode))
    return true;

  const auto input_type = GetFileType(input);
  return input_type == FILE_TYPE_PIPE || input_type == FILE_TYPE_DISK;
}

class ConsoleEchoGuard
{
public:
  ConsoleEchoGuard()
  {
    if (GetConsoleMode(input_, &mode_))
    {
      if (!SetConsoleMode(input_, mode_ & ~ENABLE_ECHO_INPUT))
        throw std::runtime_error("Could not disable console echo for OBS password input");
      active_ = true;
    }
  }
  ~ConsoleEchoGuard()
  {
    if (active_ && !SetConsoleMode(input_, mode_))
      OutputDebugStringW(L"Could not restore console echo after OBS password input.\n");
  }
  ConsoleEchoGuard(const ConsoleEchoGuard&) = delete;
  ConsoleEchoGuard& operator=(const ConsoleEchoGuard&) = delete;
  void restore()
  {
    if (!active_)
      return;
    if (!SetConsoleMode(input_, mode_))
      throw std::runtime_error("Could not restore console echo after OBS password input");
    active_ = false;
    std::cout << '\n';
  }

private:
  HANDLE input_ = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode_ = 0;
  bool active_ = false;
};

std::string read_hidden_line()
{
  ConsoleEchoGuard echo;
  std::string value;
  const bool supplied = static_cast<bool>(std::getline(std::cin, value));
  echo.restore();
  if (!supplied)
    throw std::runtime_error("No OBS password was supplied");
  return value;
}
std::string private_listeners(std::uint16_t port, bool allow_all)
{
  if (allow_all)
    return std::to_string(port) + "s";
  std::vector<std::string> values = {"127.0.0.1:" + std::to_string(port) + "s"};
  ULONG size = 0;
  GetAdaptersAddresses(AF_UNSPEC,
                       GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                       nullptr,
                       nullptr,
                       &size);
  std::vector<std::byte> storage(size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
  if (GetAdaptersAddresses(AF_UNSPEC,
                           GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                               GAA_FLAG_SKIP_DNS_SERVER,
                           nullptr,
                           adapters,
                           &size) == NO_ERROR)
    for (auto* a = adapters; a; a = a->Next)
      if (a->OperStatus == IfOperStatusUp && a->IfType != IF_TYPE_SOFTWARE_LOOPBACK)
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
        {
          char text[INET6_ADDRSTRLEN]{};
          bool private_address = false;
          if (u->Address.lpSockaddr->sa_family == AF_INET)
          {
            auto* address = &reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr;
            auto* b = reinterpret_cast<const unsigned char*>(address);
            private_address = studio_remote::is_private_listener_ipv4({b[0], b[1], b[2], b[3]});
            if (private_address && InetNtopA(AF_INET, address, text, sizeof(text)))
              values.push_back(std::string(text) + ":" + std::to_string(port) + "s");
          }
          else if (u->Address.lpSockaddr->sa_family == AF_INET6)
          {
            auto* address = &reinterpret_cast<sockaddr_in6*>(u->Address.lpSockaddr)->sin6_addr;
            auto* b = reinterpret_cast<const unsigned char*>(address);
            private_address =
                studio_remote::is_private_listener_ipv6(std::span<const std::uint8_t, 16>(b, 16));
            if (private_address && InetNtopA(AF_INET6, address, text, sizeof(text)))
              values.push_back("[" + std::string(text) + "]:" + std::to_string(port) + "s");
          }
        }
  std::string result;
  for (auto& value : values)
  {
    if (!result.empty())
      result += ",";
    result += value;
  }
  return result;
}

std::optional<std::string> read_available_command()
{
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (input == nullptr || input == INVALID_HANDLE_VALUE)
    return std::nullopt;

  DWORD console_mode = 0;
  if (GetConsoleMode(input, &console_mode))
  {
    if (!_kbhit())
      return std::nullopt;

    const int key = _getch();
    if (key == 0 || key == 0xe0)
    {
      if (_kbhit())
        static_cast<void>(_getch());
      return std::nullopt;
    }
    if (key == 'p' || key == 'v' || key == 'a' || key == 'q')
    {
      std::cout << static_cast<char>(key) << '\n';
      return std::string(1, static_cast<char>(key));
    }
    return std::nullopt;
  }

  if (WaitForSingleObject(input, 0) != WAIT_OBJECT_0)
    return std::nullopt;

  std::string command;
  if (!std::getline(std::cin, command))
    return std::string("q");
  return command;
}

#endif
} // namespace

int run_windows_companion_application(int argc, char** argv)
{
#ifdef _WIN32
  studio_remote::EnduranceProcessLog process_log;
  try
  {
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index)
      arguments.emplace_back(argv[index]);
    const auto options = studio_remote::windows::parse_startup_options(arguments);
    std::filesystem::path data_directory;
    bool open_management = false;
    bool obs_connection_overridden = false;
    bool vmix_connection_overridden = false;
    std::vector<std::string_view> built_provider_ids;
    for (const auto& provider : studio_remote::built_production_providers())
      built_provider_ids.push_back(provider.key);
    studio_remote::ProviderSelection provider_selection;
    provider_selection.ids.reserve(built_provider_ids.size());
    for (const auto id : built_provider_ids)
      provider_selection.ids.emplace_back(id);
    for (const auto& id : provider_selection.ids)
    {
      if (!provider_selection.normalized.empty())
        provider_selection.normalized.push_back(',');
      provider_selection.normalized += id;
    }
    std::optional<bool> set_sign_in_startup;
    for (const auto& option : options)
      if (option.name == "--open-management")
        open_management = true;
    for (const auto& option : options)
    {
      if (option.name != "--providers")
        continue;
      provider_selection =
          studio_remote::parse_provider_selection(option.value, built_provider_ids);
    }
    for (const auto& option : options)
    {
      if (option.name != "--set-sign-in-startup")
        continue;
      const auto value = option.value;
      if (value == "enabled")
        set_sign_in_startup = true;
      else if (value == "disabled")
        set_sign_in_startup = false;
      else
        throw std::invalid_argument("Sign-in startup must be enabled or disabled");
    }
    const bool enable_obs = provider_selection.contains("obs");
    const bool enable_vmix = provider_selection.contains("vmix");
    const bool enable_streamlabs = provider_selection.contains("streamlabs");
    const std::string configured_provider_ids = provider_selection.normalized;
    studio_remote::windows::WindowsSignInStartup sign_in_startup(configured_provider_ids);
    if (set_sign_in_startup)
    {
      sign_in_startup.set_enabled(*set_sign_in_startup);
      return 0;
    }

    studio_remote::windows::WindowsSingleInstance single_instance;
    if (!single_instance.owns_instance())
    {
      if (open_management)
      {
        single_instance.signal_management_request();
        return 0;
      }
      MessageBoxW(nullptr,
                  L"PaneLive Companion is already running.",
                  L"PaneLive Companion",
                  MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
      return 0;
    }

    const bool interactive_console = prepare_interactive_input();
    for (const auto& option : options)
      if (option.name == "--data-directory")
        data_directory = option.value;
    studio_remote::windows::ConfigStore store(data_directory);
    auto config = store.load();
    for (const auto& option : options)
    {
      const auto arg = option.name;
      if (arg == "--name")
        config.name = option.value;
      else if (arg == "--obs-password")
        studio_remote::replace_stored_password(
            config.obs_password, config.obs_password_protected, std::string(option.value));
      else if (arg == "--obs-host")
      {
        config.obs_host = option.value;
        obs_connection_overridden = true;
      }
      else if (arg == "--obs-port")
      {
        const auto value = studio_remote::windows::startup_integer(option.value, 0, 65'535);
        if (value < 1 || value > 65535)
          throw std::invalid_argument("OBS port is out of range");
        config.obs_port = static_cast<std::uint16_t>(value);
        obs_connection_overridden = true;
      }
      else if (arg == "--vmix-host")
      {
        config.vmix_host = option.value;
        vmix_connection_overridden = true;
      }
      else if (arg == "--vmix-http-port")
      {
        const auto value = studio_remote::windows::startup_integer(option.value, 0, 65'535);
        if (value < 1 || value > 65535)
          throw std::invalid_argument("vMix HTTP port is out of range");
        config.vmix_http_port = static_cast<std::uint16_t>(value);
        vmix_connection_overridden = true;
      }
      else if (arg == "--vmix-username")
        config.vmix_username = option.value;
      else if (arg == "--vmix-password")
        studio_remote::replace_stored_password(
            config.vmix_password, config.vmix_password_protected, std::string(option.value));
      else if (arg == "--vmix-program-ndi-source")
        config.vmix_program_ndi_source = option.value;
      else if (arg == "--vmix-preview-ndi-source")
        config.vmix_preview_ndi_source = option.value;
      else if (arg == "--vmix-srt-program-output")
      {
        const auto value = studio_remote::windows::startup_integer(option.value, 0, 65'535);
        if (value < 1 || value > 4)
          throw std::invalid_argument("vMix SRT Program output is out of range");
        config.vmix_srt_program_output = static_cast<std::uint8_t>(value);
      }
      else if (arg == "--vmix-srt-preview-output")
      {
        const auto value = studio_remote::windows::startup_integer(option.value, 0, 65'535);
        if (value < 1 || value > 4)
          throw std::invalid_argument("vMix SRT Preview output is out of range");
        config.vmix_srt_preview_output = static_cast<std::uint8_t>(value);
      }
      else if (arg == "--vmix-srt-program-port")
      {
        const auto value = studio_remote::windows::startup_integer(option.value, 0, 65'535);
        if (value < 1 || value > 65'535)
          throw std::invalid_argument("vMix SRT Program port is out of range");
        config.vmix_srt_program_port = static_cast<std::uint16_t>(value);
      }
      else if (arg == "--vmix-srt-preview-port")
      {
        const auto value = studio_remote::windows::startup_integer(option.value, 0, 65'535);
        if (value < 1 || value > 65'535)
          throw std::invalid_argument("vMix SRT Preview port is out of range");
        config.vmix_srt_preview_port = static_cast<std::uint16_t>(value);
      }
      else if (arg == "--vmix-srt-latency-ms")
      {
        const auto value = studio_remote::windows::startup_integer(option.value, 0, 65'535);
        if (value < 20 || value > 8'000)
          throw std::invalid_argument("vMix SRT latency is out of range");
        config.vmix_srt_latency_ms = static_cast<std::uint32_t>(value);
      }
      else if (arg == "--vmix-streaming-configured")
        config.vmix_streaming_configured = option.value == "true";
      else if (arg == "--gateway-port")
      {
        const auto value = studio_remote::windows::startup_integer(option.value, 0, 65'535);
        if (value < 1 || value > 65535)
          throw std::invalid_argument("Gateway port is out of range");
        config.port = static_cast<std::uint16_t>(value);
      }
    }
    if (obs_connection_overridden)
      config.obs_connection_configured = true;
    if (vmix_connection_overridden)
      config.vmix_connection_configured = true;
    const studio_remote::windows::VmixConnectionSettings vmix_srt_settings{
        config.vmix_host,
        config.vmix_http_port,
        config.vmix_username,
        std::nullopt,
        config.vmix_srt_program_output,
        config.vmix_srt_preview_output,
        config.vmix_srt_program_port,
        config.vmix_srt_preview_port,
        config.vmix_srt_latency_ms};
    if (!studio_remote::windows::valid_vmix_srt_settings(vmix_srt_settings))
      throw std::invalid_argument("vMix SRT outputs and ports must be distinct and valid");
    // Publish the operator's vMix location before any NDI lookup so the configured installation
    // outranks automatic detection for the whole process lifetime.
    studio_remote::windows::set_configured_vmix_installation_root(
        config.vmix_installation_directory);
    if (config.name.empty())
    {
      if (!interactive_console)
        config.name = "PaneLive Companion";
      else
      {
        std::cout << "Gateway name [PaneLive Companion]: ";
        std::getline(std::cin, config.name);
        if (config.name.empty())
          config.name = "PaneLive Companion";
      }
    }
    const auto initial_pairing_addresses = studio_remote::windows::available_lan_ipv4_addresses();
    const auto selected_pairing_address = studio_remote::select_configured_lan_ipv4(
        initial_pairing_addresses, config.pairing_address);
    config.pairing_address = selected_pairing_address.value_or("");
    if (config.obs_connection_configured && config.obs_password.empty() && interactive_console)
    {
      std::cout << "OBS WebSocket password (leave blank only if OBS authentication is disabled): ";
      studio_remote::replace_stored_password(
          config.obs_password, config.obs_password_protected, read_hidden_line());
    }
    store.save(config);
    auto* endurance_log =
        &process_log.start(config.data_directory / "endurance-logs", PROJECT_VERSION);
    studio_remote::windows::WindowsCredentialStore credentials(config.data_directory / "secrets");
    studio_remote::AuditStore audit(config.data_directory / "gateway.db", {}, endurance_log);
    studio_remote::tally::SqlitePublicationRepository tally_repository(
        config.data_directory / "gateway.db",
        [&credentials] { return credentials.uuid_v4(); },
        []
        {
          return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count());
        });
    studio_remote::tally::PublicationService tally_publications(
        tally_repository,
        credentials,
        std::string(studio_remote::tally::build::viewer_page_base_url));
    studio_remote::windows::WindowsTallyHealthProbe tally_health{
        std::string(studio_remote::tally::build::health_url)};
    const auto tally_health_result = tally_health.probe({});
    tally_publications.health_changed(tally_health_result);
    endurance_log->record(
        "remoteTally",
        "startupHealthChecked",
        {{"healthy", tally_health_result == studio_remote::tally::ServiceHealth::healthy}});
    studio_remote::teleprompter::SqliteRepository teleprompter(
        config.data_directory / "gateway.db",
        studio_remote::teleprompter::session_device_source(audit),
        [&credentials] { return credentials.uuid_v4(); },
        []
        {
          return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count());
        },
        []
        {
          return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now().time_since_epoch())
                                                .count());
        });
    studio_remote::windows::WindowsHotkeyExecutor hotkey_executor;
    if (config.enforce_single_operator_client_mode)
      audit.enforce_single_operator();
    studio_remote::GatewayIdentity identity{
        config.instance_id, config.name, config.certificate.fingerprint};
    std::vector<std::string> installed_providers;
    if (enable_obs)
      installed_providers.emplace_back("obs");
    if (enable_vmix)
      installed_providers.emplace_back("vmix");
    if (enable_streamlabs)
      installed_providers.emplace_back("streamlabs");
    installed_providers.emplace_back("companion-teleprompter");
    studio_remote::PairingManager pairing(identity, audit, credentials, installed_providers);
    studio_remote::windows::ShutdownSignal shutdown;
    auto obs_backend = std::make_unique<studio_remote::windows::ObsClient>(
        config.obs_host, config.obs_port, config.obs_password);
    auto* obs_client = obs_backend.get();
    obs_client->on_diagnostic_event(
        [&endurance_log](std::string_view event, studio_remote::json details)
        {
          if (endurance_log && endurance_log->enabled())
            endurance_log->record("obsWebSocket", event, std::move(details));
        });
    auto vmix_transport = std::make_unique<studio_remote::windows::WinHttpVmixTransport>(
        config.vmix_host, config.vmix_http_port, config.vmix_username, config.vmix_password);
    auto* vmix_client = vmix_transport.get();
    const auto make_telestrator_probe = [&config]
    {
      return std::make_unique<studio_remote::windows::WindowsVmixTelestratorProbe>(
          config.vmix_connection_configured ? config.vmix_host : "",
          config.vmix_http_port,
          config.vmix_username,
          config.vmix_password,
          config.pairing_address);
    };
    studio_remote::vmix::VmixTelestratorDiscovery vmix_telestrator(make_telestrator_probe());
    auto vmix_input_categories =
        std::make_unique<studio_remote::windows::WindowsVmixInputCategorySource>(
            studio_remote::windows::is_local_vmix_host(config.vmix_host));
    auto* vmix_input_category_source = vmix_input_categories.get();
    auto vmix_streaming_destinations =
        std::make_unique<studio_remote::windows::WindowsVmixStreamingDestinationSource>(
            studio_remote::windows::is_local_vmix_host(config.vmix_host));
    auto* vmix_streaming_destination_source = vmix_streaming_destinations.get();
    auto vmix_backend =
        std::make_unique<studio_remote::vmix::VmixAdapter>(std::move(vmix_transport),
                                                           config.vmix_streaming_configured,
                                                           std::move(vmix_input_categories),
                                                           std::move(vmix_streaming_destinations));
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    const auto streamlabs_installation =
        studio_remote::windows::detect_supported_streamlabs_installation();
    auto streamlabs_transport =
        std::make_unique<studio_remote::windows::WindowsStreamlabsNamedPipeTransport>();
    auto streamlabs_backend = std::make_unique<studio_remote::streamlabs::StreamlabsAdapter>(
        std::move(streamlabs_transport));
#endif
    studio_remote::teleprompter::Adapter teleprompter_backend;
    std::vector<studio_remote::HotkeyService::ProductionTarget> hotkey_targets;
    hotkey_targets.push_back({"companion-teleprompter", &teleprompter_backend});
    if (enable_obs)
      hotkey_targets.push_back({"obs", obs_backend.get()});
    if (enable_vmix)
      hotkey_targets.push_back({"vmix", vmix_backend.get()});
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    if (enable_streamlabs)
      hotkey_targets.push_back({"streamlabs", streamlabs_backend.get()});
#endif
    studio_remote::HotkeyService hotkeys(hotkey_executor, std::move(hotkey_targets), audit);
    auto& obs = *obs_backend;
    studio_remote::windows::WindowsThumbnailCapture thumbnail_capture;
    studio_remote::windows::WindowsThumbnailEncoder thumbnail_encoder;
    studio_remote::windows::WindowsVmixThumbnailCapture vmix_thumbnail_capture(
        config.data_directory / "vmix-thumbnails",
        [&vmix_backend](std::string_view input_id, const std::filesystem::path& path)
        {
          const auto utf8_path = path.u8string();
          vmix_backend->write_input_snapshot(input_id,
                                             std::string(utf8_path.begin(), utf8_path.end()));
        },
        studio_remote::windows::is_local_vmix_host(config.vmix_host));
    studio_remote::windows::WindowsAudioMixerRoster audio_mixer_roster;
    studio_remote::windows::WindowsSceneItemProperties scene_item_properties;
    studio_remote::windows::WindowsSourceMediaInspector source_media_inspector;
    studio_remote::GatewayRuntime runtime(identity,
                                          obs,
                                          audit,
                                          &thumbnail_capture,
                                          &thumbnail_encoder,
                                          &audio_mixer_roster,
                                          &scene_item_properties,
                                          &source_media_inspector);
    studio_remote::GatewayRuntime vmix_runtime(
        identity, *vmix_backend, audit, &vmix_thumbnail_capture, &thumbnail_encoder);
    studio_remote::GatewayRuntime teleprompter_runtime(identity, teleprompter_backend, audit);
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    studio_remote::GatewayRuntime streamlabs_runtime(identity, *streamlabs_backend, audit);
#endif
    runtime.state().subscribe(
        [&endurance_log](std::uint64_t revision, const studio_remote::json& changed)
        {
          if (endurance_log && endurance_log->enabled())
            endurance_log->record(
                "state", "canonicalStateChanged", {{"revision", revision}, {"changed", changed}});
        });
    studio_remote::windows::WindowsRealtimeAudioServices realtime_audio;
    studio_remote::WebRtcHub webrtc(runtime.state(), &realtime_audio);
    studio_remote::WebRtcHub vmix_webrtc(vmix_runtime.state(), &realtime_audio);
    // The session retains the common media owner but never starts it or negotiates peers.
    studio_remote::WebRtcHub teleprompter_webrtc(teleprompter_runtime.state());
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    studio_remote::WebRtcHub streamlabs_webrtc(streamlabs_runtime.state(), &realtime_audio);
#endif
    std::mutex audio_capability_mutex;
    std::optional<studio_remote::AudioMonitorStatus> last_vmix_monitor_capability_status;
    std::optional<studio_remote::AudioMonitorStatus> last_vmix_master_capability_status;
    std::function<void(std::string_view)> publish_provider_capabilities;
    const auto publish_audio_capabilities_if_changed =
        [&](std::string_view provider,
            std::optional<studio_remote::AudioMonitorStatus>& previous,
            const studio_remote::AudioMonitorStatus& current)
    {
      std::function<void(std::string_view)> publisher;
      {
        std::lock_guard lock(audio_capability_mutex);
        if (previous && !studio_remote::audio_capability_status_changed(*previous, current))
          return;
        previous = current;
        publisher = publish_provider_capabilities;
      }
      if (publisher)
        publisher(provider);
    };
    webrtc.on_diagnostic_event(
        [&endurance_log](std::string_view event, studio_remote::json details)
        {
          if (endurance_log && endurance_log->enabled())
            endurance_log->record("webrtc", event, std::move(details));
        });
    studio_remote::windows::WindowsAudioDiagnosticPlayer audio_diagnostics(&realtime_audio);
    studio_remote::ObsMediaDelivery media_delivery(
        runtime.state(),
        webrtc,
        [&audio_diagnostics](const studio_remote::AudioMonitorStatus& status)
        { audio_diagnostics.set_monitor_status(status); },
        endurance_log);
    studio_remote::windows::WindowsAudioMonitor audio_monitor(
        [&audio_diagnostics, &webrtc](studio_remote::SharedAudioMonitorFrame frame)
        {
          audio_diagnostics.push(frame, studio_remote::windows::AudioDiagnosticTap::pre_opus_ipc);
          webrtc.push_audio_monitor(std::move(frame));
        },
        [&media_delivery](const studio_remote::AudioMonitorStatus& status)
        { media_delivery.receive_audio_status(status); },
        &realtime_audio);
    audio_monitor.set_mix_settings(config.audio_monitor_mix);
    runtime.state().set_audio_source_audition_bus_enabled(
        config.audio_monitor_mix.activate_audio_source_audition_bus);
    webrtc.set_post_opus_diagnostic_handler(
        [&audio_diagnostics](studio_remote::SharedAudioMonitorFrame frame)
        {
          audio_diagnostics.push(std::move(frame),
                                 studio_remote::windows::AudioDiagnosticTap::post_opus);
        });
    std::unique_ptr<studio_remote::windows::WindowsManagementUi> management_ui;
    const auto create_pairing = [&](studio_remote::Role role)
    {
      auto value = pairing.open(pairing_gateway_url(config.pairing_address, config.port), role);
      const auto expiry = std::chrono::system_clock::time_point(
          std::chrono::seconds(value.at("expiresAtUnixSeconds").get<std::int64_t>()));
      return studio_remote::windows::PairingRequest{value.dump(), expiry};
    };
    const auto create_default_pairing = [&]
    {
      auto value = pairing.open_default(pairing_gateway_url(config.pairing_address, config.port),
                                        config.default_pairing_role);
      const auto expiry = std::chrono::system_clock::time_point(
          std::chrono::seconds(value.at("expiresAtUnixSeconds").get<std::int64_t>()));
      return studio_remote::windows::PairingRequest{value.dump(), expiry};
    };
    const auto open_pairing = [&](studio_remote::Role role)
    {
      auto request = create_pairing(role);
      if (!management_ui)
        throw std::runtime_error("The management interface is unavailable");
      management_ui->show_pairing(std::move(request));
      std::cout << "Pairing QR displayed in the management window for 5 minutes.\n";
    };
    studio_remote::windows::CaptureManager capture(
        [&media_delivery](std::string_view track,
                          std::shared_ptr<const studio_remote::RawVideoFrame> frame)
        { media_delivery.receive_video(track, std::move(frame)); },
        [&media_delivery](std::string_view track, const studio_remote::json& health)
        { media_delivery.receive_video_health(track, health); },
        [&webrtc] { return webrtc.video_demand(); },
        [&media_delivery](std::uint32_t generation)
        { media_delivery.receive_capture_generation(generation); });
#if defined(STUDIO_REMOTE_VMIX_VIDEO_ADAPTER_SRT) || defined(STUDIO_REMOTE_HAS_NDI)
    std::unique_ptr<studio_remote::windows::VmixVideoStreamSupervisor> vmix_capture;
    std::unique_ptr<studio_remote::windows::VmixAudioStreamSupervisor> vmix_audio_capture;
#if defined(STUDIO_REMOTE_HAS_NDI)
    auto vmix_ndi_runtime_provider = std::make_shared<studio_remote::windows::NdiRuntimeProvider>();
#endif
#if defined(STUDIO_REMOTE_VMIX_VIDEO_ADAPTER_NDI)
    studio_remote::windows::WindowsVmixOutputGeometrySource vmix_output_geometry(
        studio_remote::windows::is_local_vmix_host(config.vmix_host));
#endif
    if (enable_vmix)
    {
      const auto unsupported_audio_status = []
      {
        studio_remote::AudioMonitorStatus status;
        status.supported = false;
        status.configured = false;
        status.available = false;
        status.reason = "not-configured";
        status.sample_rate = 48'000;
        status.channels = 2;
        return status;
      };
      auto audio_status = unsupported_audio_status();
      vmix_webrtc.set_audio_monitor_status(audio_status);
      vmix_webrtc.set_audio_master_status(audio_status);

#if defined(STUDIO_REMOTE_VMIX_VIDEO_ADAPTER_SRT)
      vmix_capture = std::make_unique<studio_remote::windows::VmixVideoStreamSupervisor>(
          [&config,
           &vmix_runtime,
           &vmix_webrtc]() -> std::unique_ptr<studio_remote::vmix::IVmixVideoStreamAdapter>
          {
            const auto program_endpoint =
                studio_remote::windows::VmixSrtFeedEndpoint{config.vmix_host,
                                                            config.vmix_srt_program_port,
                                                            config.vmix_srt_program_output,
                                                            config.vmix_srt_latency_ms};
            const auto preview_endpoint =
                studio_remote::windows::VmixSrtFeedEndpoint{config.vmix_host,
                                                            config.vmix_srt_preview_port,
                                                            config.vmix_srt_preview_output,
                                                            config.vmix_srt_latency_ms};
            return std::make_unique<studio_remote::windows::VmixSrtVideoStreamAdapter>(
                program_endpoint,
                preview_endpoint,
                studio_remote::vmix::VideoStreamAdapterCallbacks{
                    [&vmix_runtime,
                     &vmix_webrtc](std::string_view track,
                                   std::shared_ptr<const studio_remote::RawVideoFrame> frame)
                    {
                      const auto captured = frame->captured_ns;
                      const auto dimensions =
                          studio_remote::encoded_video_dimensions(frame->width, frame->height);
                      vmix_webrtc.push_frame(track, std::move(frame));
                      vmix_runtime.state().set_video_health(
                          track,
                          {{"available", true},
                           {"trackEpoch", studio_remote::vmix_video_track_epoch},
                           {"ageMs", 0},
                           {"width", dimensions.width},
                           {"height", dimensions.height},
                           {"effectiveFps", vmix_webrtc.capture_target_fps(track)},
                           {"observedFrameNs", captured},
                           {"backend", "vmix-srt"}});
                    },
                    [&vmix_runtime](std::string_view track, const studio_remote::json& health)
                    {
                      studio_remote::publish_vmix_capture_health(
                          vmix_runtime.state(), track, health);
                    },
                    [&vmix_webrtc](std::string_view track)
                    { return vmix_webrtc.capture_target_fps(track); }});
          });
#elif defined(STUDIO_REMOTE_VMIX_VIDEO_ADAPTER_NDI)
      vmix_capture = std::make_unique<studio_remote::windows::VmixVideoStreamSupervisor>(
          [&config, &vmix_runtime, &vmix_webrtc, vmix_ndi_runtime_provider, &vmix_output_geometry]()
              -> std::unique_ptr<studio_remote::vmix::IVmixVideoStreamAdapter>
          {
            auto ndi_runtime = vmix_ndi_runtime_provider->acquire();
            auto sources = studio_remote::windows::select_vmix_ndi_sources(
                ndi_runtime->discover_sources(2'000, config.vmix_host),
                config.vmix_program_ndi_source,
                config.vmix_preview_ndi_source);
            if (sources.program.name.empty() || sources.preview.name.empty())
              return std::unique_ptr<studio_remote::vmix::IVmixVideoStreamAdapter>{};
            return std::make_unique<studio_remote::windows::NdiCaptureManager>(
                std::move(ndi_runtime),
                std::move(sources.program),
                std::move(sources.preview),
                studio_remote::vmix::VideoStreamAdapterCallbacks{
                    [&vmix_runtime,
                     &vmix_webrtc](std::string_view track,
                                   std::shared_ptr<const studio_remote::RawVideoFrame> frame)
                    {
                      const auto captured = frame->captured_ns;
                      const auto dimensions =
                          studio_remote::encoded_video_dimensions(frame->width, frame->height);
                      vmix_webrtc.push_frame(track, std::move(frame));
                      vmix_runtime.state().set_video_health(
                          track,
                          {{"available", true},
                           {"trackEpoch", studio_remote::vmix_video_track_epoch},
                           {"ageMs", 0},
                           {"width", dimensions.width},
                           {"height", dimensions.height},
                           {"effectiveFps", vmix_webrtc.capture_target_fps(track)},
                           {"observedFrameNs", captured},
                           {"backend", "vmix-ndi"}});
                    },
                    [&vmix_runtime](std::string_view track, const studio_remote::json& health)
                    {
                      studio_remote::publish_vmix_capture_health(
                          vmix_runtime.state(), track, health);
                    },
                    [&vmix_webrtc](std::string_view track)
                    { return vmix_webrtc.capture_target_fps(track); }},
                [&vmix_output_geometry]
                {
                  const auto geometry = vmix_output_geometry.current();
                  return geometry ? geometry->canvas_aspect_ratio() : 0.0;
                });
          });
#endif

#if defined(STUDIO_REMOTE_VMIX_AUDIO_ADAPTER_NDI)
      vmix_audio_capture = std::make_unique<studio_remote::windows::VmixAudioStreamSupervisor>(
          [&config,
           &vmix_webrtc,
           &audio_diagnostics,
           &last_vmix_monitor_capability_status,
           &last_vmix_master_capability_status,
           &publish_audio_capabilities_if_changed,
           vmix_ndi_runtime_provider]()
              -> std::unique_ptr<studio_remote::vmix::IVmixAudioStreamAdapter>
          {
            auto ndi_runtime = vmix_ndi_runtime_provider->acquire();
            auto sources = studio_remote::windows::select_vmix_ndi_sources(
                ndi_runtime->discover_sources(2'000, config.vmix_host),
                config.vmix_program_ndi_source,
                config.vmix_preview_ndi_source);
            if (sources.monitor_audio.name.empty())
              return std::unique_ptr<studio_remote::vmix::IVmixAudioStreamAdapter>{};
            return std::make_unique<studio_remote::windows::NdiAudioStreamAdapter>(
                std::move(ndi_runtime),
                std::move(sources.monitor_audio),
                std::move(sources.master_audio),
                studio_remote::vmix::AudioStreamAdapterCallbacks{
                    [&audio_diagnostics, &vmix_webrtc](std::string_view mix,
                                                       studio_remote::SharedAudioMonitorFrame frame)
                    {
                      if (mix == "monitor")
                      {
                        audio_diagnostics.push(
                            frame, studio_remote::windows::AudioDiagnosticTap::pre_opus_ipc);
                        vmix_webrtc.push_audio_monitor(std::move(frame));
                      }
                      else if (mix == "master")
                        vmix_webrtc.push_audio_master(std::move(frame));
                    },
                    [&audio_diagnostics,
                     &vmix_webrtc,
                     &last_vmix_monitor_capability_status,
                     &last_vmix_master_capability_status,
                     &publish_audio_capabilities_if_changed](
                        std::string_view mix, const studio_remote::AudioMonitorStatus& status)
                    {
                      if (mix == "monitor")
                      {
                        audio_diagnostics.set_monitor_status(status);
                        vmix_webrtc.set_audio_monitor_status(status);
                        publish_audio_capabilities_if_changed(
                            "vmix", last_vmix_monitor_capability_status, status);
                      }
                      else if (mix == "master")
                      {
                        vmix_webrtc.set_audio_master_status(status);
                        publish_audio_capabilities_if_changed(
                            "vmix", last_vmix_master_capability_status, status);
                      }
                    }});
          });
#elif defined(STUDIO_REMOTE_VMIX_AUDIO_ADAPTER_OMT)
      auto vmix_omt_runtime_provider =
          std::make_shared<studio_remote::windows::OmtRuntimeProvider>();
      vmix_audio_capture = std::make_unique<studio_remote::windows::VmixAudioStreamSupervisor>(
          [&config,
           &vmix_webrtc,
           &audio_diagnostics,
           &last_vmix_monitor_capability_status,
           &last_vmix_master_capability_status,
           &publish_audio_capabilities_if_changed,
           vmix_omt_runtime_provider]()
              -> std::unique_ptr<studio_remote::vmix::IVmixAudioStreamAdapter>
          {
            auto omt_runtime = vmix_omt_runtime_provider->acquire();
            const auto addresses = omt_runtime->discover_sources();
            const auto hostname = studio_remote::windows::local_omt_hostname();
            auto monitor_source = studio_remote::windows::select_vmix_omt_headphones_source(
                addresses, config.vmix_host, hostname);
            auto master_source = studio_remote::windows::select_vmix_omt_master_source(
                addresses, config.vmix_host, hostname);
            if (!monitor_source)
              return std::unique_ptr<studio_remote::vmix::IVmixAudioStreamAdapter>{};
            return std::make_unique<studio_remote::windows::OmtAudioStreamAdapter>(
                std::move(omt_runtime),
                std::move(*monitor_source),
                master_source ? std::move(*master_source)
                              : studio_remote::windows::OmtSourceDescriptor{},
                studio_remote::vmix::AudioStreamAdapterCallbacks{
                    [&audio_diagnostics, &vmix_webrtc](std::string_view mix,
                                                       studio_remote::SharedAudioMonitorFrame frame)
                    {
                      if (mix == "monitor")
                      {
                        audio_diagnostics.push(
                            frame, studio_remote::windows::AudioDiagnosticTap::pre_opus_ipc);
                        vmix_webrtc.push_audio_monitor(std::move(frame));
                      }
                      else if (mix == "master")
                        vmix_webrtc.push_audio_master(std::move(frame));
                    },
                    [&audio_diagnostics,
                     &vmix_webrtc,
                     &last_vmix_monitor_capability_status,
                     &last_vmix_master_capability_status,
                     &publish_audio_capabilities_if_changed](
                        std::string_view mix, const studio_remote::AudioMonitorStatus& status)
                    {
                      if (mix == "monitor")
                      {
                        audio_diagnostics.set_monitor_status(status);
                        vmix_webrtc.set_audio_monitor_status(status);
                        publish_audio_capabilities_if_changed(
                            "vmix", last_vmix_monitor_capability_status, status);
                      }
                      else if (mix == "master")
                      {
                        vmix_webrtc.set_audio_master_status(status);
                        publish_audio_capabilities_if_changed(
                            "vmix", last_vmix_master_capability_status, status);
                      }
                    }});
          });
#endif
    }
#endif
    std::function<void()> start_vmix_capture = [] {};
    std::function<void()> stop_vmix_capture = [] {};
#if defined(STUDIO_REMOTE_VMIX_VIDEO_ADAPTER_SRT) || defined(STUDIO_REMOTE_HAS_NDI)
    start_vmix_capture = [&vmix_capture, &vmix_audio_capture]
    {
      if (vmix_capture)
        vmix_capture->start();
      if (vmix_audio_capture)
        vmix_audio_capture->start();
    };
    stop_vmix_capture = [&vmix_capture, &vmix_audio_capture]
    {
      if (vmix_audio_capture)
        vmix_audio_capture->stop();
      if (vmix_capture)
        vmix_capture->stop();
    };
#endif
    const auto current_audio_monitor_status = [&] { return audio_monitor.status(); };
    webrtc.set_audio_monitor_status(current_audio_monitor_status());
    audio_diagnostics.set_monitor_status(current_audio_monitor_status());
    const auto capture_health = [&] { return capture.health(); };
    auto listener_ports = private_listeners(config.port, config.allow_non_private_interfaces);
    std::vector<studio_remote::ProviderSession> provider_sessions;
    provider_sessions.push_back(
        {"companion-teleprompter", &teleprompter_runtime, &teleprompter_webrtc});
    if (enable_obs)
      provider_sessions.push_back({"obs", &runtime, &webrtc});
    if (enable_vmix)
      provider_sessions.push_back({"vmix", &vmix_runtime, &vmix_webrtc, &vmix_telestrator});
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    if (enable_streamlabs)
      provider_sessions.push_back({"streamlabs", &streamlabs_runtime, &streamlabs_webrtc});
#endif
    studio_remote::GatewayHttpServer http(
        {config.port,
         listener_ports,
         config.certificate.server_pem_path,
         config.certificate.private_key_path,
         std::filesystem::absolute(config.data_directory / "civetweb-error.log"),
         identity},
        std::move(provider_sessions),
        pairing,
        hotkeys,
        endurance_log,
        &teleprompter,
        &tally_publications);
    publish_provider_capabilities = [&http](std::string_view provider)
    { http.publish_capabilities(provider); };
    media_delivery.on_capabilities_changed([&http] { http.publish_capabilities("obs"); });
    media_delivery.on_capture_generation_changed(
        [&http](std::uint32_t capture_generation)
        { http.reinitialize_media_sessions("obs", capture_generation); });
    studio_remote::EnduranceSnapshotRegistration endurance_snapshots(
        *endurance_log,
        studio_remote::make_obs_endurance_snapshot_provider(
            {current_audio_monitor_status,
             [&audit] { return audit.devices(); },
             studio_remote::windows::windows_host_diagnostics,
             [&runtime] { return runtime.state().snapshot(); },
             [&obs] { return obs.connected(); },
             [&runtime] { return runtime.state().fresh(); },
             [&runtime] { return runtime.last_error(); },
             [&obs] { return obs.last_error(); },
             capture_health,
             [&runtime] { return runtime.audio_mixer_diagnostics(); },
             [&runtime] { return runtime.thumbnail_diagnostics(); },
             [&runtime] { return runtime.scene_item_properties_diagnostics(); },
             [&runtime] { return runtime.critical_queue_health(); },
             [&webrtc] { return webrtc.health(); },
             [&http] { return http.diagnostics(); }}));
    audit.on_roles_changed([&http](const std::vector<studio_remote::DeviceRoleChange>& changes)
                           { http.apply_role_changes(changes); });
    studio_remote::windows::WindowsDnsSd discovery;
    studio_remote::ListenerGatewayController network(
        {studio_remote::windows::check_listener_port_availability,
         [&config](std::uint16_t port)
         { return private_listeners(port, config.allow_non_private_interfaces); },
         [&http] { return http.running(); },
         [&discovery] { discovery.stop(); },
         [&http] { http.stop(); },
         [&http, &config, &identity](std::uint16_t port, const std::string& requested_listeners)
         {
           http.configure({port,
                           requested_listeners,
                           config.certificate.server_pem_path,
                           config.certificate.private_key_path,
                           std::filesystem::absolute(config.data_directory / "civetweb-error.log"),
                           identity});
         },
         [&http] { http.start(); },
         [&discovery, &config, &configured_provider_ids](std::uint16_t port)
         {
           discovery.advertise(studio_remote::make_companion_service_record(
               {config.instance_id, config.name, config.certificate.fingerprint},
               port,
               configured_provider_ids));
         },
         [](std::string_view error)
         { std::cerr << "DNS-SD advertisement failed: " << error << '\n'; }},
        listener_ports);
    std::vector<studio_remote::AdapterGatewayBinding> adapter_gateway_bindings;
    adapter_gateway_bindings.push_back({"companion-teleprompter",
                                        [&teleprompter_runtime]
                                        { return teleprompter_runtime.running(); },
                                        [&teleprompter_runtime] { teleprompter_runtime.start(); },
                                        [&teleprompter_runtime] { teleprompter_runtime.stop(); },
                                        [] {},
                                        [] {}});
    if (enable_obs)
      adapter_gateway_bindings.push_back({"obs",
                                          [&runtime] { return runtime.running(); },
                                          [&runtime] { runtime.start(); },
                                          [&runtime] { runtime.stop(); },
                                          [&webrtc, &audio_monitor, &capture]
                                          {
                                            webrtc.start();
                                            audio_monitor.start();
                                            capture.start();
                                          },
                                          [&webrtc, &audio_monitor, &capture]
                                          {
                                            capture.stop();
                                            audio_monitor.stop();
                                            webrtc.stop();
                                          }});
    if (enable_vmix)
      adapter_gateway_bindings.push_back({"vmix",
                                          [&vmix_runtime] { return vmix_runtime.running(); },
                                          [&vmix_runtime, &vmix_telestrator]
                                          {
                                            vmix_runtime.start();
                                            vmix_telestrator.start();
                                          },
                                          [&vmix_runtime, &vmix_telestrator]
                                          {
                                            vmix_telestrator.stop();
                                            vmix_runtime.stop();
                                          },
                                          [&vmix_webrtc, &start_vmix_capture]
                                          {
                                            vmix_webrtc.start();
                                            start_vmix_capture();
                                          },
                                          [&vmix_webrtc, &stop_vmix_capture]
                                          {
                                            stop_vmix_capture();
                                            vmix_webrtc.stop();
                                          }});
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    if (enable_streamlabs)
      adapter_gateway_bindings.push_back({"streamlabs",
                                          [&streamlabs_runtime]
                                          { return streamlabs_runtime.running(); },
                                          [&streamlabs_runtime] { streamlabs_runtime.start(); },
                                          [&streamlabs_runtime] { streamlabs_runtime.stop(); },
                                          [&streamlabs_webrtc] { streamlabs_webrtc.start(); },
                                          [&streamlabs_webrtc] { streamlabs_webrtc.stop(); }});
#endif
    studio_remote::AdapterGatewayController adapter_gateways(std::move(adapter_gateway_bindings));
    adapter_gateways.start("companion-teleprompter");
    if (enable_obs)
      adapter_gateways.start("obs");
    if (enable_vmix)
      adapter_gateways.start("vmix");
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
    if (enable_streamlabs)
      adapter_gateways.start("streamlabs");
#endif
    std::cout << "Installed adapter gateways started. Each gateway reconnects independently.\n";
    if (!shutdown.wait_for(std::chrono::milliseconds(0)))
    {
      try
      {
        network.start(config.port);
      }
      catch (const std::exception& error)
      {
        std::cerr << "Companion network recovery required: " << error.what() << '\n';
      }
      studio_remote::windows::AudioDiagnosticsCallbacks audio_diagnostics_callbacks{
          [&audio_diagnostics] { return audio_diagnostics.output_devices(); },
          [&audio_diagnostics] { return audio_diagnostics.status(); },
          [&audio_diagnostics, &webrtc](std::string_view device_id,
                                        studio_remote::windows::AudioDiagnosticTap tap)
          {
            webrtc.set_post_opus_diagnostic_enabled(false);
            audio_diagnostics.start(device_id, tap);
            webrtc.set_post_opus_diagnostic_enabled(
                tap == studio_remote::windows::AudioDiagnosticTap::post_opus);
          },
          [&audio_diagnostics, &webrtc]
          {
            webrtc.set_post_opus_diagnostic_enabled(false);
            audio_diagnostics.stop();
          },
          [&audio_diagnostics](float gain_db) { audio_diagnostics.set_gain_db(gain_db); },
          [&audio_diagnostics, &runtime]
          {
            const auto state = runtime.state().snapshot();
            const auto obs_version =
                state.value("obs", studio_remote::json::object()).value("version", std::string{});
            audio_diagnostics.begin_debug_log(PROJECT_VERSION, obs_version);
          },
          [&audio_diagnostics] { audio_diagnostics.complete_debug_log(); },
          [&audio_diagnostics] { return audio_diagnostics.debug_log_active(); },
          [&audio_diagnostics] { return audio_diagnostics.debug_log_available(); },
          [&audio_diagnostics] { return audio_diagnostics.debug_log_report(); }};
      const bool network_recovery_required = !network.status().listening;
      studio_remote::ManagementStatusSnapshotBuilder status_snapshot_builder;
      studio_remote::ManagementStatusSnapshotBuilder vmix_status_snapshot_builder;
      studio_remote::ClientApprovalActions approval_actions{
          [&config] { return config.enforce_single_operator_client_mode; },
          [&config, &store, &audio_monitor, &runtime](bool enabled)
          {
            auto updated = config;
            updated.audio_monitor_mix.activate_audio_source_audition_bus = enabled;
            store.save(updated);
            config = std::move(updated);
            audio_monitor.set_mix_settings(config.audio_monitor_mix);
            runtime.state().set_audio_source_audition_bus_enabled(
                config.audio_monitor_mix.activate_audio_source_audition_bus);
          },
          [&http](std::string_view device_id,
                  std::string_view request_id,
                  studio_remote::Role role,
                  bool approved)
          { http.complete_role_change_request(device_id, request_id, role, approved); },
          [&http](std::string_view device_id, std::string_view request_id, bool approved)
          { http.complete_global_setting_change_request(device_id, request_id, approved); },
          {},
          {}};
      auto approval_callbacks =
          studio_remote::make_client_approval_callbacks(audit, approval_actions);
      management_ui = std::make_unique<studio_remote::windows::WindowsManagementUi>(
          config.name,
          PROJECT_VERSION,
          config.obs_connection_configured || config.vmix_connection_configured,
          network_recovery_required,
          config.management_interface_mode,
          [&config, &store](studio_remote::ManagementInterfaceMode mode)
          {
            auto updated = config;
            updated.management_interface_mode = mode;
            store.save(updated);
            config = std::move(updated);
          },
          [&audit, &http]
          {
            const auto paired = audit.devices();
            const auto connected = http.connected_devices();
            return studio_remote::make_management_clients(
                paired, connected, http.client_identifications());
          },
          create_default_pairing,
          [&audit, &config](std::string_view device_id)
          {
            audit.set_role(
                device_id,
                studio_remote::Role::operator_,
                config.enforce_single_operator_client_mode
                    ? studio_remote::OperatorAssignmentPolicy::transfer_existing_operators
                    : studio_remote::OperatorAssignmentPolicy::allow_multiple_operators);
          },
          [&audit](std::string_view device_id)
          { audit.set_role(device_id, studio_remote::Role::viewer); },
          [&audit, &http](std::string_view device_id)
          {
            audit.forget_device(device_id);
            http.disconnect_device(device_id);
          },
          [&config, &obs, &runtime]
          {
            auto detail = runtime.last_error();
            if (detail.empty())
              detail = obs.last_error();
            return studio_remote::ObsConnectionStatus{config.obs_host,
                                                      config.obs_port,
                                                      obs.connected(),
                                                      runtime.state().fresh(),
                                                      std::move(detail),
                                                      "OBS Studio",
                                                      true};
          },
          [&config, &store, obs_client](
              const studio_remote::windows::ObsConnectionSettings& settings)
          {
            auto updated = config;
            updated.obs_host = settings.host;
            updated.obs_port = settings.port;
            studio_remote::replace_stored_password(
                updated.obs_password, updated.obs_password_protected, settings.password);
            updated.obs_connection_configured = true;
            store.save(updated);
            config = std::move(updated);
            if (obs_client)
              obs_client->reconfigure(config.obs_host, config.obs_port, config.obs_password);
          },
          [&config, &vmix_runtime, &vmix_backend]
          {
            auto detail = vmix_runtime.last_error();
            if (detail.empty())
              detail = vmix_backend->last_error();
            return studio_remote::windows::VmixConnectionStatus{config.vmix_host,
                                                                config.vmix_http_port,
                                                                config.vmix_username,
                                                                vmix_backend->connected(),
                                                                vmix_runtime.state().fresh(),
                                                                std::move(detail),
                                                                config.vmix_srt_program_output,
                                                                config.vmix_srt_preview_output,
                                                                config.vmix_srt_program_port,
                                                                config.vmix_srt_preview_port,
                                                                config.vmix_srt_latency_ms};
          },
          [&config,
           &store,
           &vmix_thumbnail_capture,
           &vmix_telestrator,
           &make_telestrator_probe,
           vmix_client,
           &vmix_capture,
           &vmix_audio_capture,
           vmix_input_category_source,
           vmix_streaming_destination_source](
              const studio_remote::windows::VmixConnectionSettings& settings)
          {
            auto updated = config;
            updated.vmix_host = settings.host;
            updated.vmix_http_port = settings.port;
            updated.vmix_username = settings.username;
            updated.vmix_srt_program_output = settings.srt_program_output;
            updated.vmix_srt_preview_output = settings.srt_preview_output;
            updated.vmix_srt_program_port = settings.srt_program_port;
            updated.vmix_srt_preview_port = settings.srt_preview_port;
            updated.vmix_srt_latency_ms = settings.srt_latency_ms;
            studio_remote::replace_stored_password(
                updated.vmix_password, updated.vmix_password_protected, settings.password);
            updated.vmix_connection_configured = true;
            store.save(updated);
            if (vmix_audio_capture)
              vmix_audio_capture->stop();
            if (vmix_capture)
              vmix_capture->stop();
            config = std::move(updated);
            vmix_telestrator.reconfigure(make_telestrator_probe());
            if (vmix_capture)
              vmix_capture->start();
            if (vmix_audio_capture)
              vmix_audio_capture->start();
            vmix_thumbnail_capture.set_available(
                studio_remote::windows::is_local_vmix_host(config.vmix_host));
            if (vmix_input_category_source)
              vmix_input_category_source->set_available(
                  studio_remote::windows::is_local_vmix_host(config.vmix_host));
            if (vmix_streaming_destination_source)
              vmix_streaming_destination_source->set_available(
                  studio_remote::windows::is_local_vmix_host(config.vmix_host));
            if (vmix_client)
              vmix_client->reconfigure(config.vmix_host,
                                       config.vmix_http_port,
                                       config.vmix_username,
                                       config.vmix_password);
          },
          [&config, &network]
          {
            const auto status = network.status();
            return studio_remote::windows::GatewayNetworkStatus{
                config.pairing_address,
                studio_remote::windows::available_lan_ipv4_addresses(),
                config.port,
                status.listening,
                status.warning};
          },
          [&config, &store, &network, &vmix_telestrator, &make_telestrator_probe](
              const studio_remote::windows::GatewayNetworkSettings& settings)
          {
            const auto addresses = studio_remote::windows::available_lan_ipv4_addresses();
            if (std::ranges::find(addresses, settings.address) == addresses.end())
              throw std::invalid_argument(
                  "The selected pairing address is no longer active on this PC");
            if (settings.port == config.port && settings.address == config.pairing_address &&
                network.running_on(settings.port))
              return;
            auto updated = config;
            updated.pairing_address = settings.address;
            updated.port = settings.port;
            store.save(updated);
            const bool port_changed = config.port != settings.port;
            config = std::move(updated);
            vmix_telestrator.reconfigure(make_telestrator_probe());
            if (port_changed || !network.running_on(config.port))
              network.start(config.port);
          },
          [&config, &network, &obs, &vmix_backend, enable_obs, enable_vmix]
          {
            const bool companion_listening = network.status().listening;
            std::vector<studio_remote::windows::StartupPortTarget> targets;
            targets.push_back({"Companion HTTPS/WSS listener",
                               {},
                               config.port,
                               true,
                               companion_listening,
                               studio_remote::windows::StartupPortPurpose::companion_listener});
            if (enable_obs)
            {
              targets.push_back({"OBS Studio WebSocket",
                                 config.obs_host,
                                 config.obs_port,
                                 config.obs_connection_configured,
                                 obs.connected(),
                                 studio_remote::windows::StartupPortPurpose::production_backend});
            }
            if (enable_vmix)
            {
              targets.push_back({"vMix HTTP API",
                                 config.vmix_host,
                                 config.vmix_http_port,
                                 config.vmix_connection_configured,
                                 vmix_backend->connected(),
                                 studio_remote::windows::StartupPortPurpose::production_backend});
            }
            return studio_remote::windows::inspect_startup_ports(targets);
          },
          [&config,
           &network,
           &obs,
           &runtime,
           &capture_health,
           &webrtc,
           &status_snapshot_builder,
           enable_vmix,
           &vmix_webrtc,
           &vmix_status_snapshot_builder]
          {
            auto detail = runtime.last_error();
            if (detail.empty())
              detail = obs.last_error();
            const studio_remote::ObsConnectionStatus obs_status{config.obs_host,
                                                                config.obs_port,
                                                                obs.connected(),
                                                                runtime.state().fresh(),
                                                                std::move(detail),
                                                                "OBS Studio",
                                                                true};
            const auto state = runtime.state().snapshot();
            const auto obs_version =
                state.value("obs", studio_remote::json::object()).value("version", std::string{});
            auto snapshot = status_snapshot_builder.build(
                {config.name, config.port, network.status().listener_ports},
                obs_status,
                obs_version,
                capture_health(),
                runtime.audio_mixer_diagnostics(),
                runtime.thumbnail_diagnostics(),
                webrtc.health());
            if (enable_vmix)
              snapshot.feeds.insert(
                  snapshot.feeds.begin() + 2,
                  vmix_status_snapshot_builder.build_webrtc_output("vMix", vmix_webrtc.health()));
            return snapshot;
          },
          [&runtime,
           &teleprompter_runtime,
           &vmix_runtime,
           &obs,
           &vmix_backend,
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
           &streamlabs_runtime,
           &streamlabs_backend,
           &streamlabs_installation,
#endif
           enable_obs,
           enable_vmix,
           enable_streamlabs]
          {
            std::vector<studio_remote::AdapterGatewayStatus> statuses;
            statuses.push_back({"companion-teleprompter",
                                "PaneLive Teleprompter",
                                teleprompter_runtime.running(),
                                teleprompter_runtime.state().fresh(),
                                "Standalone script sharing; no production software required"});
            if (enable_obs)
            {
              auto detail = runtime.running() ? runtime.last_error() : "adapter gateway stopped";
              if (detail.empty())
                detail = obs.last_error();
              statuses.push_back({"obs",
                                  "OBS Studio",
                                  runtime.running(),
                                  runtime.running() && obs.connected(),
                                  detail});
            }
            if (enable_vmix)
            {
              auto detail =
                  vmix_runtime.running() ? vmix_runtime.last_error() : "adapter gateway stopped";
              if (detail.empty())
                detail = vmix_backend->last_error();
              statuses.push_back({"vmix",
                                  "vMix",
                                  vmix_runtime.running(),
                                  vmix_runtime.running() && vmix_backend->connected(),
                                  detail});
            }
#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
            if (enable_streamlabs)
            {
              auto detail = streamlabs_runtime.running() ? streamlabs_runtime.last_error()
                                                         : "adapter gateway stopped";
              if (detail.empty())
                detail = streamlabs_backend->last_error();
              if (detail.empty() && streamlabs_installation)
              {
                detail = "Local named pipe; Streamlabs Desktop " +
                         streamlabs_installation->version + "; " +
                         streamlabs_installation->directory.string() +
                         "; Program/layers/mute control; output observation only";
              }
              statuses.push_back({"streamlabs",
                                  "Streamlabs Desktop",
                                  streamlabs_runtime.running(),
                                  streamlabs_runtime.running() && streamlabs_backend->connected(),
                                  detail});
            }
#endif
            return statuses;
          },
          [&adapter_gateways](std::string_view provider_id,
                              studio_remote::AdapterGatewayAction action)
          {
            if (action == studio_remote::AdapterGatewayAction::start)
              adapter_gateways.start(provider_id);
            else if (action == studio_remote::AdapterGatewayAction::stop)
              adapter_gateways.stop(provider_id);
            else
              adapter_gateways.restart(provider_id);
          },
          std::move(audio_diagnostics_callbacks),
          [&config] { return config.audio_monitor_mix; },
          [&config, &store, &audio_monitor, &runtime](
              const studio_remote::AudioMonitorMixSettings& settings)
          {
            auto updated = config;
            updated.audio_monitor_mix = settings;
            store.save(updated);
            config = std::move(updated);
            audio_monitor.set_mix_settings(config.audio_monitor_mix);
            runtime.state().set_audio_source_audition_bus_enabled(
                config.audio_monitor_mix.activate_audio_source_audition_bus);
          },
          [&config]
          {
            namespace platform = studio_remote::windows;
            studio_remote::windows::ProductionInstallLocations locations;
            locations.configured_vmix_directory = config.vmix_installation_directory;
            locations.obs_directory = platform::installed_obs_directory().string();
            // Detection is reported without the configured value so the operator can compare the
            // automatic result against their override.
            const auto detected =
                platform::discover_vmix_installation_roots(std::filesystem::path{});
            if (!detected.empty())
              locations.detected_vmix_directory = detected.front().string();
            const auto runtimes = platform::discover_vmix_ndi_runtime_directories(
                std::filesystem::path(config.vmix_installation_directory));
            if (!runtimes.empty())
              locations.vmix_ndi_runtime_directory = runtimes.front().string();
            return locations;
          },
          [&config, &store, &endurance_log](std::string_view directory)
          {
            auto updated = config;
            updated.vmix_installation_directory = std::string(directory);
            store.save(updated);
            config = std::move(updated);
            // The NDI runtime is loaded once per process, so the new location applies to the next
            // Companion start. Publishing it now keeps a later lookup in this process consistent.
            studio_remote::windows::set_configured_vmix_installation_root(
                config.vmix_installation_directory);
            if (endurance_log && endurance_log->enabled())
              endurance_log->record("configuration",
                                    "vmixInstallationDirectoryChanged",
                                    {{"configured", !config.vmix_installation_directory.empty()}});
          },
          [&config] { return config.enforce_single_operator_client_mode; },
          [&config, &store, &audit, &endurance_log](bool enabled)
          {
            if (enabled)
              audit.enforce_single_operator();
            auto updated = config;
            updated.enforce_single_operator_client_mode = enabled;
            store.save(updated);
            config = std::move(updated);
            if (endurance_log && endurance_log->enabled())
              endurance_log->record(
                  "configuration", "singleOperatorClientModeChanged", {{"enabled", enabled}});
          },
          [&sign_in_startup] { return sign_in_startup.enabled(); },
          [&sign_in_startup, &endurance_log](bool enabled)
          {
            sign_in_startup.set_enabled(enabled);
            if (endurance_log && endurance_log->enabled())
              endurance_log->record(
                  "configuration", "signInStartupChanged", {{"enabled", enabled}});
          },
          [&endurance_log]
          { return endurance_log ? endurance_log->status() : studio_remote::EnduranceLogStatus{}; },
          [endurance_log](bool enabled)
          { studio_remote::apply_endurance_logging_setting(endurance_log, enabled); },
          [endurance_log] { endurance_log->clear(); },
          approval_callbacks.decide_role,
          approval_callbacks.decide_global_setting,
          [&shutdown] { shutdown.request_stop(); },
          [&http](std::string_view device_id, studio_remote::ClientIdentificationOptions options)
          { http.identify_client(device_id, options); },
          [&http](studio_remote::ClientIdentificationOptions options)
          { http.identify_all_clients(options); },
          studio_remote::make_tally_management_callbacks(
              tally_repository, tally_publications, [&http] { http.publish_capabilities("obs"); }),
          &teleprompter);
      if (open_management)
        management_ui->show_management();
      pairing.on_closed(management_ui->pairing_closed_handler());
      auto requesters = management_ui->approval_requesters();
      http.on_client_removed(requesters.cancel_device);
      approval_actions.cancel_device = std::move(requesters.cancel_device);
      approval_actions.present_role = std::move(requesters.role);
      approval_actions.present_global_setting = std::move(requesters.global_setting);
      approval_callbacks =
          studio_remote::make_client_approval_callbacks(audit, std::move(approval_actions));
      http.on_role_change_requested(approval_callbacks.request_role);
      http.on_global_setting_change_requested(approval_callbacks.request_global_setting);
      {
        if (network.status().listening)
          std::cout << "PaneLive Companion '" << config.name
                    << "' is serving installed production providers on port " << config.port
                    << ".\n";
        else
          std::cout << "PaneLive Companion '" << config.name
                    << "' is running in network recovery mode. Open Companion Network in the "
                       "management window.\n";
      }
      std::cout
          << "The notification-area icon provides management, pairing, and shutdown controls.\n"
          << "Console commands: p = operator pairing, v = viewer pairing, a = admin pairing, "
             "q = quit.\n";
      if (!interactive_console)
      {
        while (!shutdown.wait_for(std::chrono::milliseconds(50)))
          if (single_instance.consume_management_request())
            management_ui->show_management();
      }
      else
      {
        while (!shutdown.wait_for(std::chrono::milliseconds(50)))
        {
          if (single_instance.consume_management_request())
            management_ui->show_management();
          const auto command = read_available_command();
          if (!command)
            continue;
          if (*command == "q")
          {
            shutdown.request_stop();
            continue;
          }
          if (*command == "p" || *command == "v" || *command == "a")
          {
            auto role = *command == "a"   ? studio_remote::Role::admin
                        : *command == "p" ? studio_remote::Role::operator_
                                          : studio_remote::Role::viewer;
            open_pairing(role);
          }
        }
      }
    }
    http.on_role_change_requested({});
    http.on_client_removed({});
    http.on_global_setting_change_requested({});
    pairing.on_closed({});
    management_ui.reset();
    endurance_log->record("process", "componentStopping", {{"component", "gatewayRuntime"}});
    audit.on_roles_changed({});
    if (enable_obs)
      adapter_gateways.stop("obs");
    if (enable_vmix)
      adapter_gateways.stop("vmix");
    if (enable_streamlabs)
      adapter_gateways.stop("streamlabs");
    adapter_gateways.stop("companion-teleprompter");
    http.stop();
    webrtc.set_post_opus_diagnostic_enabled(false);
    audio_diagnostics.stop();
    discovery.stop();
    endurance_snapshots.reset();
    process_log.finish();
    return 0;
  }
  catch (const std::exception& e)
  {
    process_log.fail(e.what());
    std::cerr << "Gateway failed: " << e.what() << '\n';
    return 1;
  }
#else
  (void)argc;
  (void)argv;
  std::cerr << "This source tree builds portable core targets on this platform, but its production "
               "composition adapter is not enabled yet.\n";
  return 2;
#endif
}

namespace studio_remote
{
namespace
{
class WindowsCompanionApplication final : public PlatformCompanionApplication
{
public:
  int run(int argc, char** argv) override
  {
    return ::run_windows_companion_application(argc, argv);
  }
};
} // namespace

std::unique_ptr<PlatformCompanionApplication> create_platform_companion_application()
{
  return std::make_unique<WindowsCompanionApplication>();
}
} // namespace studio_remote
