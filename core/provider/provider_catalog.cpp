#include <studio_remote/provider/provider_catalog.hpp>

#include <array>

namespace studio_remote
{
namespace
{

constexpr std::array known_providers{
    ProductionProviderDescriptor{ProductionProviderId::obs,
                                 "obs",
                                 "OBS Studio",
                                 ProductionProviderStatus::implemented,
                                 NativeHelperRequirement::required,
                                 true},
    ProductionProviderDescriptor{ProductionProviderId::vmix,
                                 "vmix",
                                 "vMix",
                                 ProductionProviderStatus::implemented,
                                 NativeHelperRequirement::not_required,
                                 true},
    ProductionProviderDescriptor{ProductionProviderId::streamlabs,
                                 "streamlabs",
                                 "Streamlabs Desktop",
                                 ProductionProviderStatus::implemented,
                                 NativeHelperRequirement::not_required,
                                 true},
    ProductionProviderDescriptor{ProductionProviderId::xsplit,
                                 "xsplit",
                                 "XSplit Broadcaster",
                                 ProductionProviderStatus::research,
                                 NativeHelperRequirement::undetermined,
                                 false},
    ProductionProviderDescriptor{ProductionProviderId::wirecast,
                                 "wirecast",
                                 "Wirecast",
                                 ProductionProviderStatus::research,
                                 NativeHelperRequirement::undetermined,
                                 false},
    ProductionProviderDescriptor{ProductionProviderId::companion_teleprompter,
                                 "companion-teleprompter",
                                 "PaneLive Teleprompter",
                                 ProductionProviderStatus::implemented,
                                 NativeHelperRequirement::not_required,
                                 true}};

#if defined(STUDIO_REMOTE_ENABLE_PROVIDER_VMIX) && defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
constexpr std::array production_providers{
    known_providers[0], known_providers[1], known_providers[2]};
#elif defined(STUDIO_REMOTE_ENABLE_PROVIDER_VMIX)
constexpr std::array production_providers{known_providers[0], known_providers[1]};
#elif defined(STUDIO_REMOTE_ENABLE_PROVIDER_STREAMLABS)
constexpr std::array production_providers{known_providers[0], known_providers[2]};
#else
constexpr std::array production_providers{known_providers.front()};
#endif

constexpr auto built_providers = []
{
  std::array<ProductionProviderDescriptor, production_providers.size() + 1> result{};
  for (std::size_t index = 0; index < production_providers.size(); ++index)
    result[index] = production_providers[index];
  result.back() = known_providers.back();
  return result;
}();

} // namespace

std::span<const ProductionProviderDescriptor> known_production_providers() noexcept
{
  return known_providers;
}

std::span<const ProductionProviderDescriptor> built_production_providers() noexcept
{
  return built_providers;
}

const ProductionProviderDescriptor* find_known_production_provider(std::string_view key) noexcept
{
  for (const auto& provider : known_providers)
    if (provider.key == key)
      return &provider;
  return nullptr;
}

const ProductionProviderDescriptor* find_built_production_provider(ProductionProviderId id) noexcept
{
  for (const auto& provider : built_providers)
    if (provider.id == id)
      return &provider;
  return nullptr;
}

std::string_view production_provider_status_name(ProductionProviderStatus status) noexcept
{
  switch (status)
  {
  case ProductionProviderStatus::implemented:
    return "implemented";
  case ProductionProviderStatus::research:
    return "research";
  }
  return "unknown";
}

std::string_view native_helper_requirement_name(NativeHelperRequirement requirement) noexcept
{
  switch (requirement)
  {
  case NativeHelperRequirement::required:
    return "required";
  case NativeHelperRequirement::not_required:
    return "not-required";
  case NativeHelperRequirement::undetermined:
    return "undetermined";
  }
  return "unknown";
}

} // namespace studio_remote
