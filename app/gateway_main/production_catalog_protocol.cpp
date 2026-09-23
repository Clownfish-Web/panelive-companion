#include "production_catalog_protocol.hpp"

namespace studio_remote
{
void add_canvas_state(json& state, const std::optional<ProductionCanvasState>& canvas)
{
  if (!canvas)
    return;
  json descriptors = json::array();
  for (const auto& item : canvas->canvases)
  {
    json descriptor = {{"canvasId", item.id}, {"active", item.active}};
    if (item.width != 0)
      descriptor["width"] = item.width;
    if (item.height != 0)
      descriptor["height"] = item.height;
    descriptors.push_back(std::move(descriptor));
  }
  state["canvasState"] = {{"dualOutputEnabled", canvas->dual_output_enabled},
                          {"loading", canvas->loading},
                          {"canvases", std::move(descriptors)}};
}
std::optional<json> production_canvas_json(const ProductionCatalogSnapshot& snapshot)
{
  if (!snapshot.canvas)
    return std::nullopt;
  json state = json::object();
  add_canvas_state(state, snapshot.canvas);
  auto result = std::move(state["canvasState"]);
  result["type"] = "canvasReplacement";
  result["contractRevision"] = 2;
  result["productionProvider"] = snapshot.production_provider;
  result["stateSequence"] = snapshot.state_sequence;
  return result;
}

std::optional<json> production_collection_catalog_json(const ProductionCatalogSnapshot& snapshot)
{
  if (!snapshot.collections)
    return std::nullopt;
  const auto& catalog = *snapshot.collections;
  json choices = json::array();
  for (const auto& choice : catalog.choices)
    choices.push_back({{"collectionId", choice.id}, {"displayName", choice.name}});
  return json{{"type", "sceneCollectionReplacement"},
              {"contractRevision", 2},
              {"productionProvider", snapshot.production_provider},
              {"stateSequence", snapshot.state_sequence},
              {"currentCollectionId", catalog.selected_id},
              {"collections", std::move(choices)}};
}

std::vector<json> production_catalog_replacements(const ProductionCatalogSnapshot& snapshot)
{
  std::vector<json> result;
  if (auto canvas = production_canvas_json(snapshot))
    result.push_back(std::move(*canvas));
  if (auto collections = production_collection_catalog_json(snapshot))
    result.push_back(std::move(*collections));
  return result;
}
int production_collection_error_status(std::string_view reason)
{
  if (reason == "permission-required")
    return 403;
  if (reason == "invalid-scene-collection-selection" || reason == "invalid_production_provider")
    return 400;
  if (reason == "collection-not-found")
    return 404;
  if (reason == "state-revision-mismatch" || reason == "collection-changed" ||
      reason == "outputs-active" || reason == "collection-load-in-progress")
    return 409;
  if (reason == "collection-load-failed")
    return 502;
  return 503;
}

std::optional<ProductionCollectionSelection> parse_production_collection_selection(const json& body)
{
  if (!body.is_object())
    return std::nullopt;
  for (const auto* field : {"requestId", "collectionId", "expectedCurrentCollectionId"})
  {
    const auto value = body.find(field);
    const std::size_t maximum = std::string_view(field) == "requestId" ? 128 : 4096;
    if (value == body.end() || !value->is_string())
      return std::nullopt;
    const auto& text = value->get_ref<const std::string&>();
    if (text.empty() || text.size() > maximum || text.find('\0') != std::string::npos)
      return std::nullopt;
  }
  const auto sequence = body.find("stateSequence");
  if (sequence == body.end() || !sequence->is_number_integer() ||
      (!sequence->is_number_unsigned() && sequence->get<std::int64_t>() < 0))
    return std::nullopt;
  return ProductionCollectionSelection{body["requestId"].get<std::string>(),
                                       body["collectionId"].get<std::string>(),
                                       body["expectedCurrentCollectionId"].get<std::string>(),
                                       sequence->get<std::uint64_t>()};
}
} // namespace studio_remote
