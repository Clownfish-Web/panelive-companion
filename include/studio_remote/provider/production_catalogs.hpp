#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace studio_remote
{
struct ProductionCanvasDescriptor
{
  std::string id;
  bool available = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool active = false;
  bool operator==(const ProductionCanvasDescriptor&) const = default;
};
struct ProductionCanvasState
{
  bool dual_output_enabled = false;
  bool loading = false;
  std::vector<ProductionCanvasDescriptor> canvases;
  bool operator==(const ProductionCanvasState&) const = default;
};
struct ProductionSceneCollection
{
  std::string id;
  std::string name;
};
struct ProductionSceneCollectionCatalog
{
  std::uint64_t revision = 0;
  std::string selected_id;
  std::vector<ProductionSceneCollection> choices;
};

// Captured with the reconciled resource baseline, never projected from a newer native poll.
struct ProductionCatalogSnapshot
{
  std::uint64_t state_sequence = 0;
  std::string production_provider;
  std::optional<ProductionCanvasState> canvas;
  std::optional<ProductionSceneCollectionCatalog> collections;
  std::uint64_t baseline_sequence = 0;
};

struct ProductionCollectionSelection
{
  std::string request_id;
  std::string collection_id;
  std::string expected_current_collection_id;
  std::uint64_t state_sequence = 0;
};
class ProductionConfigurationError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};
} // namespace studio_remote
