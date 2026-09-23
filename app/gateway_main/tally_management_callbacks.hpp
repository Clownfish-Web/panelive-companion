#pragma once

#include <studio_remote/ui/management_tally.hpp>

namespace studio_remote::tally
{
class IPublicationRepository;
class PublicationService;
} // namespace studio_remote::tally

namespace studio_remote
{
// Captured dependencies outlive the native management controller and its callbacks.
RemoteTallyManagementCallbacks
make_tally_management_callbacks(tally::IPublicationRepository& repository,
                                tally::PublicationService& service,
                                std::function<void()> capabilities_changed);
} // namespace studio_remote
