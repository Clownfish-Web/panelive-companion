#pragma once

#include <studio_remote/common/types.hpp>
#include <studio_remote/provider/production_catalogs.hpp>

namespace studio_remote
{
void add_canvas_state(json& state, const std::optional<ProductionCanvasState>& canvas);
std::optional<json> production_canvas_json(const ProductionCatalogSnapshot& snapshot);
std::optional<json> production_collection_catalog_json(const ProductionCatalogSnapshot& snapshot);
std::vector<json> production_catalog_replacements(const ProductionCatalogSnapshot& snapshot);
std::optional<ProductionCollectionSelection>
parse_production_collection_selection(const json& body);
int production_collection_error_status(std::string_view reason);
} // namespace studio_remote
