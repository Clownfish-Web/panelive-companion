#pragma once

#include <algorithm>
#include <cmath>
#include <set>
#include <studio_remote/teleprompter/runtime_resources.hpp>

namespace studio_remote::teleprompter::runtime
{
template <typename Value> Result<Value> session_failure(Error error)
{
  const auto message =
      error == Error::not_found        ? "Shared teleprompter session was not found"
      : error == Error::conflict       ? "Shared teleprompter changed; reload before trying again"
      : error == Error::control_denied ? "Client cannot control this shared teleprompter"
      : error == Error::limit_reached  ? "Shared teleprompter capacity reached (64 sessions)"
      : error == Error::invalid_script
          ? "Select scripts, one master player and one controller on the same adapter"
          : "Shared teleprompter runtime is unavailable; retry the operation";
  return {{}, error, message};
}

inline bool allowed(const SessionDevice& device, SessionRole role)
{
  return std::find(device.allowed_roles.begin(), device.allowed_roles.end(), role) !=
         device.allowed_roles.end();
}

inline bool valid_assignment(const SessionDevices& devices,
                             const ScriptCatalogSnapshot& scripts,
                             std::string_view owner,
                             const SessionAssignment& assignment)
{
  const auto found = devices.find(owner);
  if (found == devices.end() || !allowed(found->second, assignment.owner_role) ||
      assignment.script_ids.empty() || assignment.script_ids.size() > 100 ||
      assignment.members.empty() || assignment.members.size() > 7)
    return false;
  int masters = assignment.owner_role == SessionRole::master ? 1 : 0;
  int controllers = assignment.owner_role == SessionRole::controller ? 1 : 0;
  std::set<std::string> unique;
  for (const auto& member : assignment.members)
  {
    const auto device = devices.find(member.device_id);
    if (device == devices.end() || device->second.provider != found->second.provider ||
        !allowed(device->second, member.role) || member.device_id == owner ||
        !unique.insert(member.device_id).second)
      return false;
    masters += member.role == SessionRole::master ? 1 : 0;
    controllers += member.role == SessionRole::controller ? 1 : 0;
  }
  if (masters != 1 || controllers != 1)
    return false;
  unique.clear();
  for (const auto& id : assignment.script_ids)
  {
    const auto script = scripts.find(id);
    if (!unique.insert(id).second || script == scripts.end() ||
        !script->second->assigned_to(found->second))
      return false;
  }
  return true;
}

inline bool valid_alignment(const SessionLineAlignment& alignment, std::string_view text)
{
  const auto start = alignment.line_start_byte_offset;
  const auto end = alignment.line_end_byte_offset;
  const auto boundary = [text](std::uint64_t offset)
  {
    return offset == text.size() ||
           (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80);
  };
  const auto maximum_lines = static_cast<double>(maximum_content_bytes + 10000);
  return alignment.line_number > 0 && alignment.line_number <= text.size() + 1 && start <= end &&
         boundary(start) && boundary(end) &&
         text.substr(start, end - start) == alignment.line_text &&
         std::isfinite(alignment.lower_edge_line) && std::isfinite(alignment.upper_edge_line) &&
         std::isfinite(alignment.lines_per_second) && alignment.lines_per_second >= 0.0 &&
         alignment.lines_per_second <= static_cast<double>(maximum_safe_integer) &&
         alignment.upper_edge_line >= -maximum_lines &&
         alignment.lower_edge_line <= maximum_lines &&
         alignment.lower_edge_line >= alignment.upper_edge_line;
}

} // namespace studio_remote::teleprompter::runtime
