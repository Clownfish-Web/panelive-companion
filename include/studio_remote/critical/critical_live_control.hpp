#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/credentials/audit_store.hpp>
#include <studio_remote/provider/production_backend.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{

enum class CriticalActionType
{
  record_start,
  record_pause,
  record_resume,
  record_stop,
  stream_start,
  stream_stop,
  stream_destination_start,
  stream_destination_stop,
  transition_trigger
};

enum class CriticalActionStatus
{
  accepted,
  executing,
  verifying,
  completed,
  rejected,
  faulted,
  canceled
};

struct CriticalAction
{
  std::string action_id;
  Device device;
  std::uint64_t client_sequence = 0;
  std::uint64_t fifo_sequence = 0;
  CriticalActionType type = CriticalActionType::record_start;
  std::string transition_id;
  std::string destination_id;
  std::uint64_t accepted_monotonic_ns = 0;
  CriticalActionStatus status = CriticalActionStatus::accepted;
  std::string reason;
  std::string message;
  json evidence = json::object();
};

struct CriticalSubmitResult
{
  int http_status = 202;
  json body = json::object();
};

std::string_view critical_action_wire_name(CriticalActionType type) noexcept;
std::string_view critical_action_status_name(CriticalActionStatus status) noexcept;
std::optional<CriticalActionType> parse_critical_action(std::string_view value);
std::vector<std::string> critical_action_capability_names(bool recording_pause_available);
std::vector<std::string>
critical_action_capability_names(const ProductionBackendCapabilities& capabilities);

} // namespace studio_remote
