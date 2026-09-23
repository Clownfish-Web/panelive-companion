#pragma once

#include <studio_remote/events/client_identification.hpp>
#include <studio_remote/ui/management_interface.hpp>
#include <studio_remote/ui/management_tally.hpp>
#include <studio_remote/ui/management_view.hpp>
#include <studio_remote/ui/teleprompter_controller.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{
struct ManagementUiCallbacks
{
  teleprompter::IRepository* teleprompter = nullptr;
  RemoteTallyManagementCallbacks remote_tally;
  std::function<std::vector<ManagementClientRow>()> clients;
  std::function<std::vector<ManagementAdapterRow>()> adapters;
  std::function<std::vector<ManagementStatusRow>()> status_feeds;
  std::function<ManagementBasicSettings()> basic_settings;
  std::function<ManagementObsConnection()> obs_connection;
  std::function<ManagementObsConnection()> streamlabs_connection;
  std::function<ManagementVmixConnection()> vmix_connection;
  std::function<ManagementNetworkSettings()> network_settings;
  std::function<ManagementEnduranceStatus()> endurance_status;
  std::function<ManagementAdvancedSettings()> advanced_settings;
  std::function<std::vector<ManagementAudioOutput>()> audio_outputs;
  std::function<ManagementAudioDiagnosticStatus()> audio_status;
  std::function<ManagementPairing()> pair_client;
  std::function<std::uint64_t()> unix_seconds;
  std::function<void()> refresh_clients;
  std::function<void(std::string_view)> set_operator;
  std::function<void(std::string_view)> set_viewer;
  std::function<void(std::string_view)> forget_client;
  std::function<void(std::string_view, ClientIdentificationOptions)> identify_client;
  std::function<void(ClientIdentificationOptions)> identify_all_clients;
  std::function<void(std::string_view)> start_adapter;
  std::function<void(std::string_view)> stop_adapter;
  std::function<void(std::string_view)> restart_adapter;
  std::function<void()> shutdown;
  std::function<void(bool)> set_single_operator_mode;
  std::function<void(bool)> set_sign_in_startup;
  std::function<void(const ManagementObsConnectionRequest&)> apply_obs_connection;
  std::function<void(const ManagementObsConnectionRequest&)> apply_streamlabs_connection;
  std::function<void(const ManagementVmixConnectionRequest&)> apply_vmix_connection;
  std::function<void(const ManagementNetworkSettingsRequest&)> apply_network_settings;
  std::function<void(bool)> set_endurance_logging;
  std::function<void()> clear_debug_logs;
  std::function<void(const ManagementAudioMixSettingsRequest&)> apply_audio_mix;
  std::function<void(std::string_view)> apply_vmix_directory;
  std::function<std::optional<std::string>()> select_vmix_directory;
  std::function<void(std::string_view, ManagementAudioDiagnosticTap)> start_audio;
  std::function<void()> stop_audio;
  std::function<void(float)> set_audio_gain;
  std::function<void()> begin_audio_debug_log;
  std::function<void()> complete_audio_debug_log;
  std::function<std::string()> audio_debug_report;
};

struct ManagementIdentificationRequest
{
  std::optional<std::string> device_id;
  std::string title;
};

struct ManagementUiState
{
  ManagementInterfaceMode interface_mode = ManagementInterfaceMode::basic;
  ManagementPage initial_page = ManagementPage::clients;
  std::vector<ManagementClientRow> clients;
  ManagementClientSort client_sort;
  std::vector<ManagementAdapterRow> adapters;
  std::vector<ManagementStatusRow> status_feeds;
  ManagementBasicSettings basic_settings;
  ManagementObsConnection obs_connection;
  ManagementObsConnection streamlabs_connection{"127.0.0.1", 59650, "", false};
  ManagementVmixConnection vmix_connection;
  ManagementNetworkSettings network_settings;
  ManagementEnduranceStatus endurance_status;
  ManagementAdvancedSettings advanced_settings;
  std::vector<ManagementAudioOutput> audio_outputs;
  ManagementAudioDiagnosticStatus audio_status;
  std::optional<ManagementPairing> pairing;
  std::optional<ManagementIdentificationRequest> identification;
  std::string last_error;
};

/// Owns framework-neutral management state, validation, and named UI commands.
/// It is confined to the native frontend thread; backend access occurs through typed callbacks.
class ManagementUiController
{
public:
  ManagementUiController(ManagementInterfaceMode mode,
                         ManagementPage initial_page,
                         std::function<void(ManagementInterfaceMode)> mode_changed,
                         ManagementUiCallbacks callbacks);

  [[nodiscard]] const ManagementUiState& state() const noexcept;
  [[nodiscard]] TeleprompterUiController& teleprompter() noexcept;
  void refresh();
  void report_error(std::string message);
  void set_interface_mode(ManagementInterfaceMode mode);
  void show_pairing(ManagementPairing pairing);
  void clear_pairing() noexcept;
  void expire_pairing();

  void pair_client();
  void refresh_clients();
  void sort_clients(ManagementClientColumn column);
  void set_operator(std::string_view device_id);
  void set_viewer(std::string_view device_id);
  void forget_client(std::string_view device_id);
  void allow_remote_tally(std::string_view device_id);
  void withdraw_remote_tally(std::string_view device_id);
  void identify_client(std::string_view device_id);
  void identify_all_clients();
  void confirm_client_identification(ClientIdentificationOptions options);
  void cancel_client_identification() noexcept;
  void start_adapter(std::string_view provider_id);
  void stop_adapter(std::string_view provider_id);
  void restart_adapter(std::string_view provider_id);
  void shutdown();
  void set_single_operator_mode(bool enabled);
  void set_sign_in_startup(bool enabled);
  void apply_obs_connection(ManagementObsConnectionRequest request);
  void apply_streamlabs_connection(ManagementObsConnectionRequest request);
  void apply_vmix_connection(ManagementVmixConnectionRequest request);
  void apply_network_settings(ManagementNetworkSettingsRequest request);
  void set_endurance_logging(bool enabled);
  void clear_debug_logs();
  void apply_audio_mix(ManagementAudioMixSettingsRequest request);
  void apply_vmix_directory(std::string_view directory);
  void start_audio(std::string_view device_id, ManagementAudioDiagnosticTap tap);
  void stop_audio();
  void set_audio_gain(float gain_db);
  void begin_audio_debug_log();
  void complete_audio_debug_log();
  [[nodiscard]] std::string audio_debug_report();

private:
  bool run(const std::function<void()>& action);
  void run_and_refresh(const std::function<void()>& action);
  [[nodiscard]] bool advanced_action_allowed();
  void refresh_remote_tally_permissions();
  void change_remote_tally_permission(
      std::string_view device_id,
      const std::function<tally::PublicationResult<bool>(std::string_view)>& action);

  ManagementUiState state_;
  std::function<void(ManagementInterfaceMode)> mode_changed_;
  ManagementUiCallbacks callbacks_;
  TeleprompterUiController teleprompter_;
};
} // namespace studio_remote
