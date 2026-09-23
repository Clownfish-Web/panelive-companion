#include "tally_management_callbacks.hpp"

#include "../../core/application/tally_publication_service.hpp"

namespace studio_remote
{
RemoteTallyManagementCallbacks
make_tally_management_callbacks(tally::IPublicationRepository& repository,
                                tally::PublicationService& service,
                                std::function<void()> capabilities_changed)
{
  return {[&repository](std::string_view id) { return repository.granted(id); },
          [&repository, &service, capabilities_changed](std::string_view id)
          {
            auto result = repository.grant(id);
            service.refresh_authorization(id);
            capabilities_changed();
            return result;
          },
          [&repository, &service, capabilities_changed](std::string_view id)
          {
            auto result = repository.withdraw(id);
            service.refresh_authorization(id);
            capabilities_changed();
            return result;
          }};
}
} // namespace studio_remote
