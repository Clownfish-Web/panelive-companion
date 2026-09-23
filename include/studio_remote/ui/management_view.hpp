#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <studio_remote/credentials/device_info.hpp>
#include <vector>

namespace studio_remote
{
enum class ManagementClientColumn
{
  status,
  client,
  backend,
  role,
  last_seen
};

enum class ManagementSortDirection
{
  ascending,
  descending
};

struct ManagementClientSort
{
  ManagementClientColumn column = ManagementClientColumn::status;
  ManagementSortDirection direction = ManagementSortDirection::descending;
};

struct ManagementClientRow
{
  std::string id;
  std::string name;
  std::string backend;
  std::string role;
  std::string last_seen;
  bool connected = false;
  bool can_set_operator = false;
  DeviceType device_type = DeviceType::unknown;
  std::string identification_code{};
  bool can_set_viewer = false;
  std::string production_provider{};
  std::uint64_t last_seen_unix_ms = 0;
  /// Unknown when the repository is unavailable or observation failed.
  std::optional<std::size_t> attached_script_count{};
  /// Unknown for unsupported providers, missing native wiring, or a failed permission read.
  std::optional<bool> remote_tally_allowed{};
};

struct ManagementAdapterRow
{
  std::string provider_id;
  std::string name;
  std::string gateway_state;
  std::string backend_state;
  std::string detail;
  bool can_start = false;
  bool can_stop = false;
  bool can_restart = false;
};

struct ManagementStatusRow
{
  std::string name;
  std::string state;
  std::string detail;
};

struct ManagementBasicSettings
{
  bool single_operator_mode = true;
  bool sign_in_startup = false;
};

struct ManagementObsConnection
{
  std::string host;
  std::uint16_t port = 4455;
  std::string status;
  bool connected = false;
};

struct ManagementObsConnectionRequest
{
  std::string host;
  std::uint16_t port = 4455;
  std::optional<std::string> password;
};

struct ManagementVmixConnection
{
  std::string host;
  std::uint16_t http_port = 8088;
  std::string username;
  std::string status;
  bool connected = false;
  std::uint8_t program_output = 1;
  std::uint16_t program_port = 10000;
  std::uint8_t preview_output = 2;
  std::uint16_t preview_port = 10002;
  std::uint16_t latency_ms = 120;
};

struct ManagementVmixConnectionRequest
{
  std::string host;
  std::uint16_t http_port = 8088;
  std::string username;
  std::optional<std::string> password;
  std::uint8_t program_output = 1;
  std::uint16_t program_port = 10000;
  std::uint8_t preview_output = 2;
  std::uint16_t preview_port = 10002;
  std::uint16_t latency_ms = 120;
};

struct ManagementNetworkSettings
{
  std::string address;
  std::vector<std::string> available_addresses;
  std::uint16_t port = 7443;
  bool listening = false;
  std::string warning;
};

struct ManagementNetworkSettingsRequest
{
  std::string address;
  std::uint16_t port = 7443;
};

struct ManagementEnduranceStatus
{
  bool enabled = false;
  std::string file;
  std::uint64_t records_written = 0;
  std::uint64_t bytes_written = 0;
  std::uint64_t queued_events = 0;
  std::uint64_t dropped_events = 0;
  std::string last_error;
  std::uint64_t retained_bytes = 0;
  std::uint64_t queued_bytes = 0;
};

struct ManagementAdvancedSettings
{
  bool automatic_headroom = true;
  std::uint32_t headroom_per_additional_source_db = 2;
  bool audio_source_audition_bus = false;
  std::string configured_vmix_directory;
  std::string detected_vmix_directory;
  std::string vmix_ndi_runtime_directory;
  std::string obs_directory;
};

struct ManagementAudioMixSettingsRequest
{
  bool automatic_headroom = true;
  std::uint32_t headroom_per_additional_source_db = 2;
  bool audio_source_audition_bus = false;
};

struct ManagementPairing
{
  std::string payload;
  std::uint64_t expires_unix_seconds = 0;
};

struct ManagementRoleChangeRequest
{
  std::string request_id;
  std::string device_id;
  std::string device_name;
  std::string requested_role;
  std::string backend_name;
};

struct ManagementGlobalSettingChangeRequest
{
  std::string request_id;
  std::string device_id;
  std::string device_name;
  std::string setting_id;
  std::string setting_name;
  std::string warning;
  bool requested_value = false;
};

using ManagementApprovalDecision = std::function<void(bool approved)>;

enum class ManagementAudioDiagnosticTap
{
  pre_opus,
  post_opus
};

struct ManagementAudioOutput
{
  std::string id;
  std::string name;
  bool is_default = false;
};

struct ManagementAudioDiagnosticStatus
{
  bool playing = false;
  ManagementAudioDiagnosticTap tap = ManagementAudioDiagnosticTap::pre_opus;
  std::string device_name;
  std::string error;
  std::uint64_t received_frames = 0;
  std::uint64_t played_frames = 0;
  std::uint64_t queue_overflows = 0;
  std::uint64_t output_underruns = 0;
  std::uint64_t discontinuities = 0;
  std::uint64_t silent_concealment_frames = 0;
  std::size_t queued_frames = 0;
  float gain_db = 0.0F;
  float maximum_peak_last_second = 0.0F;
  float average_peak_last_second = 0.0F;
  bool duration_limit_reached = false;
  bool debug_log_active = false;
  bool debug_log_available = false;
};
} // namespace studio_remote
