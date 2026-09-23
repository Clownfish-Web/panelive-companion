#include <studio_remote/streamlabs/streamlabs_rpc_transport.hpp>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <studio_remote/streamlabs/streamlabs_direct_catalogs.hpp>
#include <studio_remote/streamlabs/streamlabs_protocol.hpp>
#include <utility>

namespace studio_remote::streamlabs
{
class StreamlabsRpcTransport::Impl
{
public:
  explicit Impl(std::unique_ptr<IStreamlabsRpcChannel> channel) : channel_(std::move(channel))
  {
    if (!channel_)
      throw std::invalid_argument("Streamlabs RPC channel is required");
  }
  ~Impl()
  {
    disconnect();
  }
  void connect()
  {
    std::lock_guard lock(mutex_);
    if (channel_->connected())
      return;
    channel_->connect();
    next_request_id_ = 2;
    scene_catalog_ = json::array();
    next_hotkeys_ = {};
    next_program_probe_ = {};
  }
  void disconnect() noexcept
  {
    std::lock_guard lock(mutex_);
    channel_->disconnect();
  }
  bool connected() const noexcept
  {
    std::lock_guard lock(mutex_);
    return channel_->connected();
  }
  std::string desktop_version() const
  {
    std::lock_guard lock(mutex_);
    return channel_->desktop_version();
  }
  State read_state()
  {
    std::lock_guard lock(mutex_);
    require_connected();
    State result;
    result.desktop_version = channel_->desktop_version();
    result.active_collection =
        parse_active_collection(call("SceneCollectionsService", "activeCollection"));
    if (result.active_collection.id != collection_id_)
    {
      collection_id_ = result.active_collection.id;
      scene_catalog_ = json::array();
      next_hotkeys_ = {};
      next_program_probe_ = {};
    }
    try
    {
      result.collections = parse_collections(call("SceneCollectionsService", "collections"),
                                             result.active_collection);
    }
    catch (const std::exception&)
    {
      result.collections.reset();
    }
    // Native display/Studio state can change without a subscription notification.
    // Every control refresh must observe availability before addressing a placement.
    {
      try
      {
        canvas_ = parse_canvas_state(call("DualOutputService", "state"));
      }
      catch (const std::exception&)
      {
        canvas_.reset();
      }
      studio_mode_ = parse_studio_mode(call("TransitionsService", "getModel"));
    }
    result.canvas = canvas_;
    result.studio_mode_enabled = studio_mode_;
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_hotkeys_)
    {
      configured_actions_ = read_actions();
      next_hotkeys_ = now + std::chrono::seconds(30);
    }
    result.configured_actions = configured_actions_;
    try
    {
      result.destinations = parse_destinations(call("StreamSettingsService", "state"));
    }
    catch (const std::exception&)
    {
      result.destinations.reset();
    }
    if (scene_catalog_.empty() || now >= next_scene_catalog_)
    {
      scene_catalog_ = call("ScenesService", "getScenes");
      next_scene_catalog_ = now + std::chrono::seconds(30);
    }
    result.active_scene_id = parse_active_scene_id(call("ScenesService", "activeScene"));
    const auto source_types = parse_source_types(call("SourcesService", "getSources"));
    const auto scenes = scene_catalog_;
    for (const auto& scene : scenes)
    {
      const auto id = scene.at("id").get<std::string>();
      result.scenes.push_back(
          {id,
           scene.at("name").get<std::string>(),
           parse_scene_layers(
               call(scene.at("resourceId").get<std::string>(), "getItems"), id, source_types)});
    }
    if (std::ranges::any_of(result.scenes,
                            [](const auto& scene)
                            {
                              return std::ranges::any_of(scene.nodes,
                                                         [](const auto& node)
                                                         {
                                                           return node.canvas != "horizontal" &&
                                                                  node.canvas != "vertical";
                                                         });
                            }))
      result.canvas.reset();
    result.audio_sources = parse_audio_sources(call("AudioService", "getSourcesForCurrentScene"));
    const auto [recording, streaming] = parse_output_states(call("StreamingService", "getModel"));
    result.recording = recording;
    result.streaming = streaming;
    result.performance = parse_performance_state(call("PerformanceService", "getModel"));
    result.program_scene_id = result.active_scene_id;
    if (result.studio_mode_enabled)
    {
      result.preview_scene_id = result.active_scene_id;
      result.program_scene_id.clear();
      if (now >= next_program_probe_)
      {
        try
        {
          const auto id = parse_active_scene_id(call("TransitionsService", "currentSceneId"));
          if (!find_scene(result, id))
            throw std::runtime_error("Unknown Streamlabs Program scene");
          result.program_scene_id = id;
        }
        catch (const std::exception&)
        {
          next_program_probe_ = now + std::chrono::seconds(5);
        }
      }
    }
    result.program_identity_available = !result.program_scene_id.empty();
    if (!find_scene(result, result.active_scene_id))
      throw std::runtime_error("Streamlabs active scene is absent from the complete scene catalog");
    return result;
  }
  std::optional<std::vector<ConfiguredAction>> read_configured_actions()
  {
    std::lock_guard lock(mutex_);
    configured_actions_ = read_actions();
    return configured_actions_;
  }
  void set_studio_mode(bool enabled)
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(
        call("TransitionsService", enabled ? "enableStudioMode" : "disableStudioMode"));
  }
  void select_collection(std::string_view id)
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(call("SceneCollectionsService", "load", json::array({id})));
    scene_catalog_ = json::array();
  }

  std::string read_program_scene_id()
  {
    std::lock_guard lock(mutex_);
    require_connected();
    const auto id = parse_active_scene_id(call("TransitionsService", "currentSceneId"));
    next_program_probe_ = {};
    return id;
  }

  bool read_studio_mode_enabled()
  {
    std::lock_guard lock(mutex_);
    require_connected();
    return parse_studio_mode(call("TransitionsService", "getModel"));
  }

  studio_remote::streamlabs::PerformanceState read_performance()
  {
    std::lock_guard lock(mutex_);
    require_connected();
    return parse_performance_state(call("PerformanceService", "getModel"));
  }

  std::string read_active_scene_id()
  {
    std::lock_guard lock(mutex_);
    require_connected();
    return parse_active_scene_id(call("ScenesService", "activeSceneId"));
  }

  std::pair<studio_remote::streamlabs::OutputState, studio_remote::streamlabs::OutputState>
  read_output_states()
  {
    std::lock_guard lock(mutex_);
    require_connected();
    return parse_output_states(call("StreamingService", "getModel"));
  }

  bool read_scene_item_visibility(std::string_view resource_id)
  {
    std::lock_guard lock(mutex_);
    require_connected();
    return parse_scene_item_visibility(call(resource_id, "getModel"));
  }

  studio_remote::streamlabs::AudioSource read_audio_source(std::string_view resource_id)
  {
    std::lock_guard lock(mutex_);
    require_connected();
    return parse_audio_source(call(resource_id, "getModel"), resource_id);
  }

  void select_program_scene(std::string_view scene_id)
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(call("ScenesService", "makeSceneActive", json::array({scene_id})));
  }

  void select_preview_scene(std::string_view scene_id)
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(call("ScenesService", "makeSceneActive", json::array({scene_id})));
  }

  void set_scene_item_visibility(std::string_view resource_id, bool visible)
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(call(resource_id, "setVisibility", json::array({visible})));
  }

  void set_audio_mute(std::string_view resource_id, bool muted)
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(call(resource_id, "setMuted", json::array({muted})));
  }

  void set_audio_volume(std::string_view resource_id, double volume_db)
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(
        call(resource_id,
             "setDeflection",
             json::array({studio_remote::streamlabs::volume_db_to_deflection(volume_db)})));
  }

  void toggle_recording()
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(call("StreamingService", "toggleRecording"));
  }

  void toggle_streaming()
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(call("StreamingService", "toggleStreaming"));
  }

  void execute_studio_transition()
  {
    std::lock_guard lock(mutex_);
    static_cast<void>(call("TransitionsService", "executeStudioModeTransition"));
  }

  void subscribe(EventCallback callback)
  {
    std::lock_guard lock(mutex_);
    require_connected();
    event_callback_ = std::move(callback);
    for (const auto resource : studio_remote::streamlabs::event_allowlist())
    {
      const auto separator = resource.find('.');
      if (separator == std::string_view::npos)
        throw std::logic_error("Streamlabs event allowlist entry is invalid");
      try
      {
        static_cast<void>(call(resource.substr(0, separator), resource.substr(separator + 1)));
      }
      catch (const std::exception&)
      {
        if (!resource.starts_with("DualOutputService."))
          throw;
      }
    }
  }

private:
  std::optional<std::vector<ConfiguredAction>> read_actions()
  {
    try
    {
      return parse_configured_actions(call("HotkeysService", "getHotkeysSet"));
    }
    catch (const std::exception&)
    {
      return std::nullopt;
    }
  }
  void require_connected() const
  {
    if (!channel_->connected())
      throw std::runtime_error("Streamlabs transport is disconnected");
  }
  json call(std::string_view resource, std::string_view method, json arguments = json::array())
  {
    const auto request_id = next_request_id_++;
    std::optional<std::string> promise_id;
    auto wire = make_request(request_id, resource, method, std::move(arguments)).dump();
    if (wire.size() > maximum_message_bytes)
      throw std::runtime_error("Streamlabs request exceeds the configured limit");
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(method == "load" ? 10 : 5);
    channel_->write_message(wire);
    while (true)
    {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      if (remaining <= std::chrono::milliseconds::zero())
        throw std::runtime_error("Streamlabs request timed out");
      auto message = parse_message(channel_->read_message(remaining));
      if (message.event)
      {
        if (message.promise_event)
        {
          const auto disposition =
              classify_promise_event(message, promise_id ? *promise_id : std::string_view{});
          if (disposition == PromiseEventDisposition::unrelated)
            continue;
          if (disposition == PromiseEventDisposition::rejected)
          {
            throw std::runtime_error("Streamlabs asynchronous request failed");
          }
          return std::move(message.event_data);
        }
        if (message.event_resource.starts_with("SceneCollectionsService.") ||
            message.event_resource == "ScenesService.sceneAdded" ||
            message.event_resource == "ScenesService.sceneRemoved")
          scene_catalog_ = json::array();
        if (event_callback_)
          event_callback_(message.event_resource, message.event_data);
        continue;
      }
      if (!message.id || *message.id != request_id)
        throw std::runtime_error("Streamlabs returned an unexpected correlation ID");
      if (const auto subscription = promise_subscription_id(message.result))
      {
        promise_id = *subscription;
        continue;
      }
      return std::move(message.result);
    }
  }

  mutable std::mutex mutex_;
  std::unique_ptr<IStreamlabsRpcChannel> channel_;
  std::uint64_t next_request_id_ = 2;
  EventCallback event_callback_;
  bool studio_mode_ = false;
  std::optional<CanvasState> canvas_;
  std::optional<std::vector<ConfiguredAction>> configured_actions_;
  json scene_catalog_ = json::array();
  std::string collection_id_;
  std::chrono::steady_clock::time_point next_hotkeys_, next_scene_catalog_, next_program_probe_;
};

StreamlabsRpcTransport::StreamlabsRpcTransport(std::unique_ptr<IStreamlabsRpcChannel> channel)
    : impl_(std::make_unique<Impl>(std::move(channel)))
{
}

StreamlabsRpcTransport::~StreamlabsRpcTransport() = default;

void StreamlabsRpcTransport::connect()
{
  impl_->connect();
}

void StreamlabsRpcTransport::disconnect() noexcept
{
  impl_->disconnect();
}

bool StreamlabsRpcTransport::connected() const noexcept
{
  return impl_->connected();
}

std::string StreamlabsRpcTransport::desktop_version() const
{
  return impl_->desktop_version();
}

studio_remote::streamlabs::State StreamlabsRpcTransport::read_state()
{
  return impl_->read_state();
}

bool StreamlabsRpcTransport::read_studio_mode_enabled()
{
  return impl_->read_studio_mode_enabled();
}

std::string StreamlabsRpcTransport::read_active_scene_id()
{
  return impl_->read_active_scene_id();
}

std::pair<studio_remote::streamlabs::OutputState, studio_remote::streamlabs::OutputState>
StreamlabsRpcTransport::read_output_states()
{
  return impl_->read_output_states();
}

studio_remote::streamlabs::PerformanceState StreamlabsRpcTransport::read_performance()
{
  return impl_->read_performance();
}

bool StreamlabsRpcTransport::read_scene_item_visibility(std::string_view resource_id)
{
  return impl_->read_scene_item_visibility(resource_id);
}

studio_remote::streamlabs::AudioSource
StreamlabsRpcTransport::read_audio_source(std::string_view resource_id)
{
  return impl_->read_audio_source(resource_id);
}

std::string StreamlabsRpcTransport::read_program_scene_id()
{
  return impl_->read_program_scene_id();
}

void StreamlabsRpcTransport::select_program_scene(std::string_view scene_id)
{
  impl_->select_program_scene(scene_id);
}

void StreamlabsRpcTransport::select_preview_scene(std::string_view scene_id)
{
  impl_->select_preview_scene(scene_id);
}

void StreamlabsRpcTransport::set_scene_item_visibility(std::string_view resource_id, bool visible)
{
  impl_->set_scene_item_visibility(resource_id, visible);
}

void StreamlabsRpcTransport::set_audio_mute(std::string_view resource_id, bool muted)
{
  impl_->set_audio_mute(resource_id, muted);
}

void StreamlabsRpcTransport::set_audio_volume(std::string_view resource_id, double volume_db)
{
  impl_->set_audio_volume(resource_id, volume_db);
}

void StreamlabsRpcTransport::toggle_recording()
{
  impl_->toggle_recording();
}

void StreamlabsRpcTransport::toggle_streaming()
{
  impl_->toggle_streaming();
}

void StreamlabsRpcTransport::execute_studio_transition()
{
  impl_->execute_studio_transition();
}

void StreamlabsRpcTransport::subscribe(EventCallback callback)
{
  impl_->subscribe(std::move(callback));
}

std::optional<std::vector<ConfiguredAction>> StreamlabsRpcTransport::read_configured_actions()
{
  return impl_->read_configured_actions();
}
void StreamlabsRpcTransport::set_studio_mode(bool enabled)
{
  impl_->set_studio_mode(enabled);
}
void StreamlabsRpcTransport::select_collection(std::string_view id)
{
  impl_->select_collection(id);
}
} // namespace studio_remote::streamlabs
