#pragma once

#include <studio_remote/ui/management_status.hpp>
#include <studio_remote/ui/management_view.hpp>

#include <vector>

namespace studio_remote
{
/// Presentation-only projection of ordered health feeds; owns no observation history.
std::vector<ManagementStatusRow>
make_management_status_rows(const ManagementStatusSnapshot& status);
} // namespace studio_remote
