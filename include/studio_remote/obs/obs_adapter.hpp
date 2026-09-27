#pragma once
#include <future>
#include <optional>
#include <source_location>
#include <string_view>
#include <studio_remote/common/types.hpp>
#include <studio_remote/input/hotkey.hpp>
#include <studio_remote/provider/production_backend.hpp>
#include <studio_remote/provider/production_catalogs.hpp>
#include <utility>
#include <vector>

namespace studio_remote
{
struct NormalizedObsEvent
{
  std::string type;
  json data;
  std::uint64_t observed_ns = 0;
};
class IObsAdapter
{
public:
  using EventCallback = std::function<void(const NormalizedObsEvent&)>;
  using ConnectionCallback = std::function<void(bool, std::string_view)>;
  virtual ~IObsAdapter() = default;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual bool connected() const = 0;
  virtual json request(std::string_view type,
                       json data = {},
                       std::source_location caller = std::source_location::current(),
                       std::string_view reason = {}) = 0;
  // The synchronous call owns the authority predicate until dispatch returns. Adapters that
  // queue before reaching the provider must carry it through that queue and recheck at dispatch.
  virtual std::optional<json>
  request_authorized(std::string_view type,
                     json data,
                     const std::function<bool()>& authorized,
                     std::source_location caller = std::source_location::current(),
                     std::string_view reason = {})
  {
    if (!authorized || !authorized())
      return std::nullopt;
    return request(type, std::move(data), caller, reason);
  }
  virtual json request_current(std::string_view type,
                               json data = {},
                               std::source_location caller = std::source_location::current(),
                               std::string_view reason = {})
  {
    return request(type, std::move(data), caller, reason);
  }
  virtual void on_event(EventCallback callback) = 0;
  virtual void on_connection(ConnectionCallback callback) = 0;
  virtual ProductionBackendInfo backend_info() const
  {
    return {};
  }
  virtual std::string last_error() const
  {
    return {};
  }
  virtual bool recording_pause_available() const
  {
    return backend_info().capabilities.recording_pause;
  }
  virtual void trigger_hotkey(const ProductionHotkeyTrigger&)
  {
    throw HotkeyExecutionError("production-hotkey-unsupported",
                               "The production backend does not support hotkey triggering");
  }
  virtual std::optional<std::vector<ProductionHotkeyDefinition>> hotkeys()
  {
    return std::nullopt;
  }
  virtual std::optional<ProductionCanvasState> canvas_state() const
  {
    return std::nullopt;
  }
  virtual std::optional<ProductionSceneCollectionCatalog> scene_collection_catalog()
  {
    return std::nullopt;
  }
  virtual bool select_scene_collection(std::string_view, std::uint64_t)
  {
    throw ProductionConfigurationError("scene-collections-unsupported");
  }
  virtual bool hotkey_catalog_supported() const noexcept
  {
    return false;
  }
};
} // namespace studio_remote
