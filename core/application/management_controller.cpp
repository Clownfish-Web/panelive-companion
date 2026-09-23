#include <studio_remote/ui/management_clients.hpp>
#include <studio_remote/ui/management_controller.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace studio_remote
{
namespace
{
std::uint64_t system_unix_seconds()
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count());
}

bool valid_host(std::string_view host)
{
  return !host.empty() && host.size() <= 253 &&
         std::ranges::none_of(host, [](unsigned char value) { return value <= 0x20; });
}

bool valid_vmix_request(const ManagementVmixConnectionRequest& request)
{
  const bool outputs_valid = request.program_output >= 1 && request.program_output <= 4 &&
                             request.preview_output >= 1 && request.preview_output <= 4 &&
                             request.program_output != request.preview_output;
  return valid_host(request.host) && outputs_valid &&
         request.program_port != request.preview_port &&
         request.program_port != request.http_port && request.preview_port != request.http_port &&
         request.latency_ms >= 20 && request.latency_ms <= 8'000;
}
} // namespace

ManagementUiController::ManagementUiController(
    ManagementInterfaceMode mode,
    ManagementPage initial_page,
    std::function<void(ManagementInterfaceMode)> mode_changed,
    ManagementUiCallbacks callbacks)
    : mode_changed_(std::move(mode_changed)), callbacks_(std::move(callbacks)),
      teleprompter_(callbacks_.teleprompter)
{
  state_.interface_mode = mode;
  state_.initial_page = visible_management_page(mode, initial_page);
}

const ManagementUiState& ManagementUiController::state() const noexcept
{
  return state_;
}

TeleprompterUiController& ManagementUiController::teleprompter() noexcept
{
  return teleprompter_;
}

void ManagementUiController::refresh()
{
  std::string attachment_error;
  run(
      [this, &attachment_error]
      {
        if (callbacks_.clients)
          state_.clients = callbacks_.clients();
        sort_management_client_rows(state_.clients, state_.client_sort);
        if (callbacks_.adapters)
          state_.adapters = callbacks_.adapters();
        if (callbacks_.status_feeds)
          state_.status_feeds = callbacks_.status_feeds();
        if (callbacks_.basic_settings)
          state_.basic_settings = callbacks_.basic_settings();
        if (callbacks_.obs_connection)
          state_.obs_connection = callbacks_.obs_connection();
        if (callbacks_.streamlabs_connection)
          state_.streamlabs_connection = callbacks_.streamlabs_connection();
        if (callbacks_.vmix_connection)
          state_.vmix_connection = callbacks_.vmix_connection();
        if (callbacks_.network_settings)
          state_.network_settings = callbacks_.network_settings();
        if (callbacks_.endurance_status)
          state_.endurance_status = callbacks_.endurance_status();
        if (callbacks_.advanced_settings)
          state_.advanced_settings = callbacks_.advanced_settings();
        if (callbacks_.audio_outputs)
          state_.audio_outputs = callbacks_.audio_outputs();
        if (callbacks_.audio_status)
          state_.audio_status = callbacks_.audio_status();
        expire_pairing();
        teleprompter_.update_clients(state_.clients);
        teleprompter_.refresh();
        for (auto& client : state_.clients)
        {
          client.attached_script_count.reset();
          if (!callbacks_.teleprompter)
            continue;
          const auto scripts = callbacks_.teleprompter->list(client.id);
          if (scripts)
            client.attached_script_count = scripts.value->size();
          else if (attachment_error.empty())
            attachment_error = "Could not read client script attachments: " + scripts.message;
        }
      });
  if (!attachment_error.empty())
    state_.last_error += (state_.last_error.empty() ? "" : "\n") + attachment_error;
  refresh_remote_tally_permissions();
}

void ManagementUiController::report_error(std::string message)
{
  state_.last_error = std::move(message);
}

void ManagementUiController::set_interface_mode(ManagementInterfaceMode mode)
{
  if (mode == state_.interface_mode)
    return;
  run(
      [this, mode]
      {
        if (mode_changed_)
          mode_changed_(mode);
        state_.interface_mode = mode;
        state_.initial_page = visible_management_page(mode, state_.initial_page);
      });
}

void ManagementUiController::show_pairing(ManagementPairing pairing)
{
  state_.pairing = std::move(pairing);
  expire_pairing();
}

void ManagementUiController::clear_pairing() noexcept
{
  state_.pairing.reset();
}

void ManagementUiController::expire_pairing()
{
  if (!state_.pairing)
    return;
  const auto now = callbacks_.unix_seconds ? callbacks_.unix_seconds() : system_unix_seconds();
  if (now >= state_.pairing->expires_unix_seconds)
    state_.pairing.reset();
}

void ManagementUiController::pair_client()
{
  run(
      [this]
      {
        if (callbacks_.pair_client)
          show_pairing(callbacks_.pair_client());
      });
}

void ManagementUiController::refresh_clients()
{
  run_and_refresh(callbacks_.refresh_clients);
}

void ManagementUiController::sort_clients(ManagementClientColumn column)
{
  auto& sort = state_.client_sort;
  if (sort.column == column)
    sort.direction = sort.direction == ManagementSortDirection::ascending
                         ? ManagementSortDirection::descending
                         : ManagementSortDirection::ascending;
  else
  {
    sort.column = column;
    sort.direction =
        column == ManagementClientColumn::status || column == ManagementClientColumn::last_seen
            ? ManagementSortDirection::descending
            : ManagementSortDirection::ascending;
  }
  sort_management_client_rows(state_.clients, sort);
}

void ManagementUiController::set_operator(std::string_view device_id)
{
  if (!device_id.empty() && callbacks_.set_operator)
    run_and_refresh([this, device_id] { callbacks_.set_operator(device_id); });
}

void ManagementUiController::set_viewer(std::string_view device_id)
{
  if (!device_id.empty() && callbacks_.set_viewer)
    run_and_refresh([this, device_id] { callbacks_.set_viewer(device_id); });
}

void ManagementUiController::forget_client(std::string_view device_id)
{
  if (!device_id.empty() && callbacks_.forget_client)
    run_and_refresh([this, device_id] { callbacks_.forget_client(device_id); });
}

void ManagementUiController::identify_client(std::string_view device_id)
{
  if (state_.identification)
    return;
  const auto client = std::ranges::find(state_.clients, device_id, &ManagementClientRow::id);
  if (device_id.empty() || client == state_.clients.end() || !client->connected)
  {
    state_.last_error = "Client is offline or not ready to identify";
    return;
  }
  if (!callbacks_.identify_client)
  {
    state_.last_error = "Client identification is unavailable";
    return;
  }
  state_.last_error.clear();
  state_.identification = ManagementIdentificationRequest{std::string(device_id),
                                                          "Identify " + client->name + " (" +
                                                              client->identification_code + ")"};
}

void ManagementUiController::identify_all_clients()
{
  if (state_.identification)
    return;
  if (std::ranges::none_of(state_.clients, &ManagementClientRow::connected))
  {
    state_.last_error = "No online clients are ready to identify";
    return;
  }
  if (!callbacks_.identify_all_clients)
  {
    state_.last_error = "Client identification is unavailable";
    return;
  }
  state_.last_error.clear();
  state_.identification =
      ManagementIdentificationRequest{std::nullopt, "Identify all online clients"};
}

void ManagementUiController::confirm_client_identification(ClientIdentificationOptions options)
{
  const auto request = std::exchange(state_.identification, std::nullopt);
  if (!request)
    return;
  const bool online = std::ranges::any_of(
      state_.clients,
      [&](const auto& client)
      { return client.connected && (!request->device_id || client.id == *request->device_id); });
  if (!online)
  {
    state_.last_error = request->device_id ? "Client is offline or not ready to identify"
                                           : "No online clients are ready to identify";
    return;
  }
  run_and_refresh(
      [&]
      {
        if (request->device_id)
          callbacks_.identify_client(*request->device_id, options);
        else
          callbacks_.identify_all_clients(options);
      });
}

void ManagementUiController::cancel_client_identification() noexcept
{
  state_.identification.reset();
}

void ManagementUiController::start_adapter(std::string_view provider_id)
{
  if (!provider_id.empty() && callbacks_.start_adapter)
    run_and_refresh([this, provider_id] { callbacks_.start_adapter(provider_id); });
}

void ManagementUiController::stop_adapter(std::string_view provider_id)
{
  if (!provider_id.empty() && callbacks_.stop_adapter)
    run_and_refresh([this, provider_id] { callbacks_.stop_adapter(provider_id); });
}

void ManagementUiController::restart_adapter(std::string_view provider_id)
{
  if (!provider_id.empty() && callbacks_.restart_adapter)
    run_and_refresh([this, provider_id] { callbacks_.restart_adapter(provider_id); });
}

void ManagementUiController::shutdown()
{
  run(callbacks_.shutdown);
}

void ManagementUiController::set_single_operator_mode(bool enabled)
{
  if (callbacks_.set_single_operator_mode)
    run_and_refresh([this, enabled] { callbacks_.set_single_operator_mode(enabled); });
}

void ManagementUiController::set_sign_in_startup(bool enabled)
{
  if (callbacks_.set_sign_in_startup)
    run_and_refresh([this, enabled] { callbacks_.set_sign_in_startup(enabled); });
}

void ManagementUiController::apply_obs_connection(ManagementObsConnectionRequest request)
{
  if (!valid_host(request.host))
  {
    state_.last_error = "Enter a valid OBS host";
    return;
  }
  if (callbacks_.apply_obs_connection)
    run_and_refresh([this, request = std::move(request)]
                    { callbacks_.apply_obs_connection(request); });
}

void ManagementUiController::apply_streamlabs_connection(ManagementObsConnectionRequest request)
{
  if (request.host != "127.0.0.1" || request.port == 0 ||
      (request.password && (request.password->empty() || request.password->size() > 4096 ||
                            request.password->find('\0') != std::string::npos)))
  {
    state_.last_error = "Streamlabs connection settings are invalid";
    return;
  }
  if (callbacks_.apply_streamlabs_connection)
    run_and_refresh([this, request = std::move(request)]
                    { callbacks_.apply_streamlabs_connection(request); });
}

void ManagementUiController::apply_vmix_connection(ManagementVmixConnectionRequest request)
{
  if (!valid_vmix_request(request))
  {
    state_.last_error = "vMix connection and SRT output settings are invalid";
    return;
  }
  if (callbacks_.apply_vmix_connection)
    run_and_refresh([this, request = std::move(request)]
                    { callbacks_.apply_vmix_connection(request); });
}

void ManagementUiController::apply_network_settings(ManagementNetworkSettingsRequest request)
{
  if (!advanced_action_allowed())
    return;
  const auto address =
      std::ranges::find(state_.network_settings.available_addresses, request.address);
  if (request.address.empty() || address == state_.network_settings.available_addresses.end())
  {
    state_.last_error = "Select an active pairing address";
    return;
  }
  if (callbacks_.apply_network_settings)
    run_and_refresh([this, request = std::move(request)]
                    { callbacks_.apply_network_settings(request); });
}

void ManagementUiController::set_endurance_logging(bool enabled)
{
  if (!advanced_action_allowed())
    return;
  if (callbacks_.set_endurance_logging)
    run_and_refresh([this, enabled] { callbacks_.set_endurance_logging(enabled); });
}

void ManagementUiController::clear_debug_logs()
{
  if (advanced_action_allowed() && callbacks_.clear_debug_logs)
    run_and_refresh([this] { callbacks_.clear_debug_logs(); });
}

void ManagementUiController::apply_audio_mix(ManagementAudioMixSettingsRequest request)
{
  if (state_.interface_mode == ManagementInterfaceMode::basic)
  {
    request.automatic_headroom = state_.advanced_settings.automatic_headroom;
    request.headroom_per_additional_source_db =
        state_.advanced_settings.headroom_per_additional_source_db;
  }
  if (request.headroom_per_additional_source_db < 1 ||
      request.headroom_per_additional_source_db > 4)
  {
    state_.last_error = "Headroom attenuation must be between 1 and 4 dB";
    return;
  }
  if (callbacks_.apply_audio_mix)
    run_and_refresh([this, request] { callbacks_.apply_audio_mix(request); });
}

void ManagementUiController::apply_vmix_directory(std::string_view directory)
{
  if (!advanced_action_allowed())
    return;
  if (callbacks_.apply_vmix_directory)
    run_and_refresh([this, directory] { callbacks_.apply_vmix_directory(directory); });
}

void ManagementUiController::start_audio(std::string_view device_id,
                                         ManagementAudioDiagnosticTap tap)
{
  if (!advanced_action_allowed() || device_id.empty())
    return;
  if (callbacks_.start_audio)
    run_and_refresh([this, device_id, tap] { callbacks_.start_audio(device_id, tap); });
}

void ManagementUiController::stop_audio()
{
  if (!advanced_action_allowed())
    return;
  run_and_refresh(callbacks_.stop_audio);
}

void ManagementUiController::set_audio_gain(float gain_db)
{
  if (!advanced_action_allowed())
    return;
  const auto bounded = std::clamp(gain_db, -24.0F, 12.0F);
  if (callbacks_.set_audio_gain)
    run_and_refresh([this, bounded] { callbacks_.set_audio_gain(bounded); });
}

void ManagementUiController::begin_audio_debug_log()
{
  if (advanced_action_allowed())
    run_and_refresh(callbacks_.begin_audio_debug_log);
}

void ManagementUiController::complete_audio_debug_log()
{
  if (advanced_action_allowed())
    run_and_refresh(callbacks_.complete_audio_debug_log);
}

std::string ManagementUiController::audio_debug_report()
{
  if (!advanced_action_allowed() || !callbacks_.audio_debug_report)
    return {};
  std::string report;
  run([this, &report] { report = callbacks_.audio_debug_report(); });
  return report;
}

bool ManagementUiController::run(const std::function<void()>& action)
{
  if (!action)
    return true;
  try
  {
    action();
    state_.last_error.clear();
    return true;
  }
  catch (const std::exception& error)
  {
    state_.last_error = error.what();
  }
  catch (...)
  {
    state_.last_error = "Management operation failed";
  }
  return false;
}

void ManagementUiController::run_and_refresh(const std::function<void()>& action)
{
  if (run(action))
  {
    refresh();
    return;
  }
  // A callback can commit before a later step fails. Observe the actual state without losing
  // that action's error, and retain a secondary observation failure if refresh also fails.
  auto action_error = std::move(state_.last_error);
  refresh();
  if (!state_.last_error.empty())
    action_error += "\nRefresh failed: " + state_.last_error;
  state_.last_error = std::move(action_error);
}

bool ManagementUiController::advanced_action_allowed()
{
  if (state_.interface_mode == ManagementInterfaceMode::advanced)
    return true;
  state_.last_error = "Switch to the Advanced interface to use this setting";
  return false;
}
} // namespace studio_remote
