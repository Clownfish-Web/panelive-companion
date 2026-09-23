#pragma once

#include <span>
#include <string_view>

namespace studio_remote
{

enum class ProductionProviderId
{
  obs,
  vmix,
  streamlabs,
  xsplit,
  wirecast,
  companion_teleprompter
};

enum class ProductionProviderStatus
{
  implemented,
  research
};

enum class NativeHelperRequirement
{
  required,
  not_required,
  undetermined
};

struct ProductionProviderDescriptor
{
  ProductionProviderId id;
  std::string_view key;
  std::string_view display_name;
  ProductionProviderStatus status;
  NativeHelperRequirement native_helper;
  bool installable;
};

std::span<const ProductionProviderDescriptor> known_production_providers() noexcept;
std::span<const ProductionProviderDescriptor> built_production_providers() noexcept;
const ProductionProviderDescriptor* find_known_production_provider(std::string_view key) noexcept;
const ProductionProviderDescriptor*
find_built_production_provider(ProductionProviderId id) noexcept;
std::string_view production_provider_status_name(ProductionProviderStatus status) noexcept;
std::string_view native_helper_requirement_name(NativeHelperRequirement requirement) noexcept;

} // namespace studio_remote
