#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/credentials/device_info.hpp>
#include <studio_remote/events/client_identification.hpp>

#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <studio_remote/ui/management_view.hpp>
#include <vector>

namespace studio_remote
{
struct Device;

struct ManagementClient
{
  std::string id;
  std::string name;
  Role role = Role::viewer;
  std::uint64_t last_seen_unix_ms = 0;
  bool connected = false;
  std::string production_provider = "obs";
  std::string production_backend_name = "OBS Studio";
  DeviceType device_type = DeviceType::unknown;
  std::string identification_code{};
};

/// Joins the already observed paired and connected snapshots; retains paired identities/roles.
std::vector<ManagementClient>
make_management_clients(std::span<const Device> paired,
                        std::span<const Device> connected,
                        std::span<const ClientIdentification> identifications);

std::vector<ManagementClientRow>
make_management_client_rows(std::vector<ManagementClient> clients,
                            std::chrono::system_clock::time_point now);

/// Sorts presentation rows without parsing relative time labels. Ties use name/backend/ID.
void sort_management_client_rows(std::vector<ManagementClientRow>& rows, ManagementClientSort sort);

} // namespace studio_remote
