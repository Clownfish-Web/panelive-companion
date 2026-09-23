#include <studio_remote/vmix/vmix_adapter.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <map>
#include <random>
#include <stdexcept>

namespace studio_remote::vmix
{
namespace
{

constexpr std::size_t maximum_hotkeys = 4096;
constexpr std::size_t maximum_hotkey_id_bytes = 128;
constexpr std::size_t maximum_hotkey_name_bytes = 512;
constexpr std::string_view built_in_cut_transition_id = "vmix-cut";
constexpr std::uint32_t vmix_maximum_stream_destinations = 5;

std::string decode_shortcut_text(std::string_view html)
{
  std::string text;
  bool in_tag = false;
  for (std::size_t index = 0; index < html.size(); ++index)
  {
    if (html[index] == '<')
    {
      if (index + 2 < html.size() &&
          std::tolower(static_cast<unsigned char>(html[index + 1])) == 'b' &&
          std::tolower(static_cast<unsigned char>(html[index + 2])) == 'r' && !text.empty() &&
          text.back() != ' ')
        text.push_back(' ');
      in_tag = true;
      continue;
    }
    if (in_tag)
    {
      if (html[index] == '>')
        in_tag = false;
      continue;
    }
    if (html[index] == '&')
    {
      const auto end = html.find(';', index + 1);
      if (end != std::string_view::npos && end - index <= 6)
      {
        const auto entity = html.substr(index, end - index + 1);
        if (entity == "&amp;")
          text.push_back('&');
        else if (entity == "&lt;")
          text.push_back('<');
        else if (entity == "&gt;")
          text.push_back('>');
        else if (entity == "&quot;")
          text.push_back('"');
        else if (entity == "&#39;" || entity == "&apos;")
          text.push_back('\'');
        else
          text.append(entity);
        index = end;
        continue;
      }
    }
    text.push_back(html[index]);
  }

  std::string normalized;
  for (const auto character : text)
  {
    if (std::isspace(static_cast<unsigned char>(character)))
    {
      if (!normalized.empty() && normalized.back() != ' ')
        normalized.push_back(' ');
    }
    else
      normalized.push_back(character);
  }
  while (!normalized.empty() && normalized.back() == ' ')
    normalized.pop_back();
  return normalized;
}

std::string shortcut_name(std::string_view content)
{
  const auto key_end = content.find("</strong>");
  if (key_end == std::string_view::npos)
    return decode_shortcut_text(content);
  const auto name_start = content.find('>', key_end + std::string_view("</strong>").size());
  if (name_start == std::string_view::npos)
    return decode_shortcut_text(content);
  const auto name_end = content.find('<', name_start + 1);
  if (name_end == std::string_view::npos)
    return decode_shortcut_text(content.substr(name_start + 1));
  const auto name = decode_shortcut_text(content.substr(name_start + 1, name_end - name_start - 1));
  return name.empty() ? decode_shortcut_text(content) : name;
}

std::vector<ProductionHotkeyDefinition> parse_shortcuts(std::string_view page)
{
  std::vector<ProductionHotkeyDefinition> result;
  std::size_t position = 0;
  while (result.size() < maximum_hotkeys)
  {
    const auto identifier_start = page.find("?shortcut=", position);
    if (identifier_start == std::string_view::npos)
      break;
    const auto value_start = identifier_start + std::string_view("?shortcut=").size();
    const auto value_end = page.find_first_of("'\"&<", value_start);
    if (value_end == std::string_view::npos)
      break;
    position = value_end;

    const auto text_class = page.find("shortcutButtonText", value_end);
    const auto next_shortcut = page.find("?shortcut=", value_end);
    if (text_class == std::string_view::npos ||
        (next_shortcut != std::string_view::npos && text_class > next_shortcut))
      continue;
    const auto text_start = page.find('>', text_class);
    const auto text_end = page.find("</div>", text_start);
    if (text_start == std::string_view::npos || text_end == std::string_view::npos)
      continue;

    const auto identifier = page.substr(value_start, value_end - value_start);
    const auto content = page.substr(text_start + 1, text_end - text_start - 1);
    const auto name = shortcut_name(content);
    if (identifier.empty() || identifier.size() > maximum_hotkey_id_bytes || name.empty() ||
        name.size() > maximum_hotkey_name_bytes)
      continue;

    std::vector<std::string> keys;
    const auto key_start = content.find("<strong>");
    const auto key_end = content.find("</strong>", key_start);
    if (key_start != std::string_view::npos && key_end != std::string_view::npos)
    {
      const auto key_text = decode_shortcut_text(
          content.substr(key_start + std::string_view("<strong>").size(),
                         key_end - key_start - std::string_view("<strong>").size()));
      if (const auto key = parse_hotkey_key(key_text); key && !is_hotkey_modifier(*key))
        keys.push_back(hotkey_key_name(*key));
    }
    if (std::none_of(result.begin(),
                     result.end(),
                     [&](const ProductionHotkeyDefinition& item) { return item.id == identifier; }))
      result.push_back({std::string(identifier), name, std::move(keys)});
  }
  return result;
}

std::string required_string(const json& value, std::string_view name)
{
  const auto found = value.find(std::string(name));
  if (found == value.end() || !found->is_string() || found->get<std::string>().empty())
    throw std::invalid_argument("vMix request requires " + std::string(name));
  return found->get<std::string>();
}

const Input& selected_input(const State& state, const json& selector)
{
  const auto uuid = selector.value("inputUuid", "");
  const auto name = selector.value("inputName", "");
  const auto* input = find_input(state, !uuid.empty() ? uuid : name);
  if (!input)
    throw std::out_of_range("vMix input was not found");
  return *input;
}

const Input& selected_scene(const State& state, const json& selector)
{
  const auto uuid = selector.value("sceneUuid", "");
  const auto name = selector.value("sceneName", "");
  const auto* input = find_input(state, !uuid.empty() ? uuid : name);
  if (!input)
    throw std::out_of_range("vMix composition input was not found");
  return *input;
}

double volume_multiplier(double percentage)
{
  return std::clamp(percentage, 0.0, 100.0) / 100.0;
}

double volume_db(double percentage)
{
  const auto multiplier = volume_multiplier(percentage);
  return multiplier <= 0.0 ? -100.0 : std::max(-100.0, 20.0 * std::log10(multiplier));
}

double fader_percentage(double decibels)
{
  if (decibels <= VmixMasterMute::silence_db)
    return 0.0;
  const auto amplitude = std::pow(10.0, decibels / 20.0);
  return std::clamp(std::pow(amplitude, 0.25) * 100.0, 0.0, 100.0);
}

bool contains_layer(const Input& input, std::string_view key)
{
  return std::any_of(input.layers.begin(),
                     input.layers.end(),
                     [&](const Layer& layer) { return layer.input_key == key; });
}

bool active_in_program(const State& state, const Input& candidate)
{
  if (candidate.audio_capable && candidate.audio_buses.find('M') != std::string::npos)
    return true;
  const auto* active = find_input(state, state.active_input);
  return (active && (active->key == candidate.key || contains_layer(*active, candidate.key))) ||
         std::find(state.overlay_inputs.begin(), state.overlay_inputs.end(), candidate.number) !=
             state.overlay_inputs.end();
}

bool routed_to_master(const Input& input)
{
  return input.audio_capable && input.audio_buses.find('M') != std::string::npos;
}

std::string_view generic_kind(std::string_view native_type)
{
  if (native_type == "Audio")
    return "audio";
  if (native_type == "Image" || native_type == "Photos" || native_type == "PowerPoint" ||
      native_type == "Colour" || native_type == "Color")
    return "image";
  if (native_type == "Title" || native_type == "Xaml" || native_type == "GT" ||
      native_type == "Text")
    return "text";
  if (native_type == "Browser" || native_type == "WebBrowser")
    return "browser";
  if (native_type == "Capture" || native_type == "Video" || native_type == "VLC" ||
      native_type == "NDI" || native_type == "DesktopCapture" || native_type == "VirtualSet" ||
      native_type == "Stream" || native_type == "DVD")
    return "video";
  return "unknown";
}

json scene_items(const State& state, const Input& composition)
{
  json items = json::array();
  const auto append = [&](std::int64_t item_id,
                          const Input& input,
                          std::string stable_id,
                          bool base,
                          std::uint32_t order,
                          bool visible,
                          bool visibility_controllable,
                          bool visibility_state_available,
                          bool isolated_thumbnail_available)
  {
    items.push_back({{"sceneItemId", item_id},
                     {"stableLayerId", std::move(stable_id)},
                     {"sourceName", input.title},
                     {"sourceUuid", input.key},
                     {"inputKind", input.type},
                     {"genericKind", generic_kind(input.type)},
                     {"sourceType", "VMIX_INPUT"},
                     {"sceneItemEnabled", visible},
                     {"baseLayer", base},
                     {"layerOrder", order},
                     {"visibilityControllable", visibility_controllable},
                     {"visibilityStateAvailable", visibility_state_available},
                     {"thumbnailAvailable", isolated_thumbnail_available},
                     {"thumbnailUnavailableReason",
                      isolated_thumbnail_available ? "" : "isolated-base-unavailable"},
                     {"color", input.color.empty() ? json(nullptr) : json(input.color)},
                     {"isGroup", false},
                     {"audioCapable", input.audio_capable},
                     {"visualCapability", input.type == "Audio" ? "audio-only" : "visual"}});
  };
  append(0,
         composition,
         "vmix-base:" + composition.key,
         true,
         0,
         true,
         false,
         true,
         composition.layers.empty());
  std::map<std::string, std::size_t> occurrences;
  for (const auto& layer : composition.layers)
    if (const auto* input = find_input(state, layer.input_key))
    {
      const auto occurrence = occurrences[input->key]++;
      append(layer.index,
             *input,
             "vmix-layer:" + composition.key + ":" + input->key + ":" + std::to_string(occurrence),
             false,
             layer.index,
             layer.visible,
             true,
             layer.visibility_known,
             true);
    }
  return items;
}

bool input_roster_equal(const State& left, const State& right)
{
  if (left.inputs.size() != right.inputs.size() || left.transitions != right.transitions)
    return false;
  for (std::size_t index = 0; index < left.inputs.size(); ++index)
    if (left.inputs[index].key != right.inputs[index].key ||
        left.inputs[index].number != right.inputs[index].number)
      return false;
  return true;
}

json streaming_destinations(const State& state)
{
  const bool streaming_configured = state.stream_configuration_override;
  json destinations = json::array();
  if (!state.streaming_destinations.empty())
  {
    for (const auto& destination : state.streaming_destinations)
    {
      const auto stream = destination.stream;
      const auto label = destination.name_or_key.empty()
                             ? "vMix Stream " + std::to_string(stream + 1)
                             : destination.name_or_key;
      const auto lifecycle_state = !destination.activity_known ? "unknown"
                                   : destination.active        ? "streaming"
                                                               : "stopped";
      destinations.push_back(
          {{"destinationId", "vmix-stream-" + std::to_string(stream)},
           {"backendId", std::to_string(stream)},
           {"key", label},
           {"label", label},
           {"host",
            destination.host.empty() ? "vmix-stream-" + std::to_string(stream + 1)
                                     : destination.host},
           {"serviceKind", destination.service_kind},
           {"order", stream},
           {"configured", destination.configured},
           {"enabled", destination.enabled},
           {"state", lifecycle_state},
           {"controlScope", "individual"},
           {"failureReason", destination.activity_known ? json(nullptr) : json("unknown")}});
    }
    return destinations;
  }

  destinations.push_back({{"destinationId", "vmix-stream-0"},
                          {"backendId", "0"},
                          {"label", "vMix Stream 1"},
                          {"host", streaming_configured ? "vmix-stream-1" : "not-configured"},
                          {"serviceKind", "custom"},
                          {"order", 0},
                          {"configured", streaming_configured},
                          {"enabled", streaming_configured},
                          {"state", state.streaming ? "streaming" : "stopped"},
                          {"controlScope", "individual"},
                          {"failureReason", nullptr}});
  return destinations;
}

std::uint32_t stream_number_from_destination_id(const json& data)
{
  const auto id = data.value("destinationId", data.value("streamDestinationId", std::string{}));
  constexpr std::string_view prefix = "vmix-stream-";
  if (id.size() <= prefix.size() || id.rfind(prefix, 0) != 0)
    throw std::invalid_argument("vMix stream destinationId is invalid");
  std::uint32_t result = 0;
  const auto digits = std::string_view(id).substr(prefix.size());
  const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() ||
      result >= vmix_maximum_stream_destinations)
    throw std::invalid_argument("vMix stream destinationId is invalid");
  return result;
}

NormalizedObsEvent event(std::string type, json data)
{
  return {std::move(type), std::move(data), monotonic_ns()};
}

} // namespace

VmixAdapter::VmixAdapter(std::unique_ptr<IVmixTransport> transport,
                         bool streaming_configured,
                         std::unique_ptr<IVmixInputCategorySource> input_categories,
                         std::unique_ptr<IVmixStreamingDestinationSource> streaming_destinations)
    : transport_(std::move(transport)), input_categories_(std::move(input_categories)),
      streaming_destinations_(std::move(streaming_destinations)),
      streaming_configured_(streaming_configured)
{
  if (!transport_)
    throw std::invalid_argument("vMix transport is required");
}

VmixAdapter::~VmixAdapter()
{
  stop();
}

void VmixAdapter::start()
{
  std::lock_guard lock(mutex_);
  if (worker_.joinable())
    return;
  stopping_ = false;
  worker_ = std::thread([this] { run(); });
}

void VmixAdapter::stop()
{
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

bool VmixAdapter::connected() const
{
  std::lock_guard lock(mutex_);
  return connected_;
}

void VmixAdapter::on_event(EventCallback callback)
{
  std::lock_guard lock(mutex_);
  event_callback_ = std::move(callback);
}

void VmixAdapter::on_connection(ConnectionCallback callback)
{
  std::lock_guard lock(mutex_);
  connection_callback_ = std::move(callback);
}

ProductionBackendInfo VmixAdapter::backend_info() const
{
  const bool local_preset_available = input_categories_ && input_categories_->available();
  std::lock_guard lock(mutex_);
  return {"vmix",
          "vMix",
          {.native_audio_roster_required = false,
           .layer_visibility = true,
           .layer_colors = local_preset_available,
           .audio_meters = true,
           .audio_monitoring = false,
           .audio_input_positive_gain = false,
           .audio_solo = true,
           .audio_input_balance = true,
           .audio_input_master_routing = true,
           .audio_master_volume = true,
           .audio_master_mute = true,
           .audio_source_audition_bus = false,
           .audio_volume_minimum_db = -100.0,
           .audio_volume_maximum_db = 0.0,
           .audio_volume_step_percent = 1,
           .audio_volume_fader_exponent = 4.0,
           .recording_pause = false,
           .recording_observe = true,
           .recording_start_stop = true,
           .streaming_observe = true,
           .streaming_start_stop = true,
           .streaming_destinations = true,
           .streaming_multistream = true,
           .streaming_destination_individual_control = true,
           .streaming_destination_maximum = vmix_maximum_stream_destinations,
           .stream_configuration_known = state_.stream_configuration_known,
           .quick_transitions = true,
           .default_transition_uses_quick_play = true,
           .default_transition_state_available = false,
           .default_transition_configuration_available = false,
           .transition_completion_requires_video = false,
           .stable_command_identifiers_preferred = true,
           .authoritative_control_verification = true,
           .explicit_start_stop_for_record = true,
           .explicit_start_stop_for_stream = true,
           .performance = false,
           .performance_metrics = {}}};
}

std::string VmixAdapter::last_error() const
{
  std::lock_guard lock(mutex_);
  return last_error_;
}

void VmixAdapter::write_input_snapshot(std::string_view input_id, std::string_view output_path)
{
  if (output_path.empty())
    throw std::invalid_argument("vMix thumbnail output path is required");
  {
    std::lock_guard lock(mutex_);
    if (!connected_)
      throw std::runtime_error("vMix is not connected");
    if (!find_input(state_, input_id))
      throw std::out_of_range("vMix thumbnail input was not found");
  }
  std::lock_guard lock(transport_mutex_);
  transport_->call("SnapshotInput",
                   {{"Input", std::string(input_id)}, {"Value", std::string(output_path)}});
}

void VmixAdapter::reconfigure(std::unique_ptr<IVmixTransport> transport, bool streaming_configured)
{
  if (!transport)
    throw std::invalid_argument("vMix transport is required");
  {
    std::lock_guard transport_lock(transport_mutex_);
    transport_ = std::move(transport);
    master_mute_.reset();
  }
  ConnectionCallback connection;
  {
    std::lock_guard lock(mutex_);
    streaming_configured_ = streaming_configured;
    if (connected_)
      connection = connection_callback_;
    connected_ = false;
    layer_visibility_confirmations_.clear();
    last_error_ = "vMix connection settings changed; reconnecting";
  }
  if (connection)
    connection(false, {});
  wake_.notify_all();
}

State VmixAdapter::refresh()
{
  State state;
  bool configuration_override = false;
  {
    std::lock_guard lock(mutex_);
    configuration_override = streaming_configured_;
  }
  {
    std::lock_guard lock(transport_mutex_);
    state = parse_state(transport_->read_state());
    if (state.master_volume)
    {
      const auto db = volume_db(*state.master_volume);
      master_mute_.observe(db);
      state.master_muted = master_mute_.muted(db);
    }
  }
  if (input_categories_ && input_categories_->available())
    if (const auto preset = input_categories_->read_current_preset())
      apply_local_preset_metadata(state, *preset);
  if (!state.stream_configuration_known && configuration_override)
  {
    state.stream_configuration_known = true;
    state.stream_configuration_override = true;
  }
  if (streaming_destinations_ && streaming_destinations_->available())
  {
    if (const auto configured = streaming_destinations_->read_destinations())
    {
      state.stream_configuration_known = true;
      state.stream_configuration_override = false;
      state.streaming_destinations.clear();
      state.streaming_destinations.reserve(configured->size());
      for (const auto& destination : *configured)
        state.streaming_destinations.push_back({destination.stream,
                                                destination.name_or_key,
                                                destination.host,
                                                destination.service_kind,
                                                true,
                                                true,
                                                false,
                                                false});
    }
  }
  return state;
}

void VmixAdapter::run()
{
  auto retry_delay = std::chrono::milliseconds(250);
  std::minstd_rand random(static_cast<unsigned int>(monotonic_ns()));
  std::uniform_int_distribution<int> jitter_percent(-10, 10);
  while (true)
  {
    {
      std::lock_guard lock(mutex_);
      if (stopping_)
        return;
    }

    try
    {
      publish(refresh());
      ConnectionCallback connection;
      std::string version;
      {
        std::lock_guard lock(mutex_);
        version = state_.version;
        last_error_.clear();
        if (!connected_)
        {
          connected_ = true;
          connection = connection_callback_;
        }
      }
      if (connection)
        connection(true, version);
      retry_delay = std::chrono::milliseconds(250);
    }
    catch (const std::exception& error)
    {
      ConnectionCallback connection;
      {
        std::lock_guard lock(mutex_);
        last_error_ = std::string(error.what()).substr(0, 240);
        if (connected_)
        {
          connected_ = false;
          connection = connection_callback_;
        }
      }
      if (connection)
        connection(false, {});
      retry_delay = std::min(retry_delay * 2, std::chrono::milliseconds(5'000));
    }

    const auto wait_delay = retry_delay + retry_delay * jitter_percent(random) / 100;
    std::unique_lock lock(mutex_);
    if (wake_.wait_for(lock, wait_delay, [this] { return stopping_; }))
      return;
  }
}

void VmixAdapter::publish(State replacement, bool provider_observation)
{
  State previous;
  EventCallback callback;
  bool was_connected = false;
  {
    std::lock_guard lock(mutex_);
    apply_layer_visibility_confirmations(replacement, provider_observation);
    apply_streaming_destination_activity(replacement, provider_observation);
    previous = state_;
    state_ = replacement;
    callback = event_callback_;
    was_connected = connected_;
  }
  // An established connection may have no usable inputs while every input is offline. Its
  // recovery still needs a roster event; only initial connection defers to the startup baseline.
  if (!callback || !was_connected)
    return;

  if (!input_roster_equal(previous, replacement))
    callback(event("SceneListChanged", {}));
  else
    for (const auto& input : replacement.inputs)
      if (const auto* old = find_input(previous, input.key);
          old && scene_items(previous, *old) != scene_items(replacement, input))
        callback(event("CompositionLayersChanged",
                       {{"sceneName", input.title},
                        {"sceneUuid", input.key},
                        {"sceneItems", scene_items(replacement, input)}}));

  const auto* old_program = find_input(previous, previous.active_input);
  const auto* new_program = find_input(replacement, replacement.active_input);
  if ((!old_program && new_program) ||
      (old_program && new_program && old_program->key != new_program->key))
    callback(event("CurrentProgramSceneChanged",
                   {{"sceneName", new_program->title}, {"sceneUuid", new_program->key}}));

  const auto* old_preview = find_input(previous, previous.preview_input);
  const auto* new_preview = find_input(replacement, replacement.preview_input);
  if ((!old_preview && new_preview) ||
      (old_preview && new_preview && old_preview->key != new_preview->key))
    callback(event("CurrentPreviewSceneChanged",
                   {{"sceneName", new_preview->title}, {"sceneUuid", new_preview->key}}));

  for (const auto& input : replacement.inputs)
  {
    const auto* old = find_input(previous, input.key);
    if (!old)
      continue;
    if (old->muted != input.muted)
      callback(event(
          "InputMuteStateChanged",
          {{"inputName", input.title}, {"inputUuid", input.key}, {"inputMuted", input.muted}}));
    if (old->solo != input.solo)
      callback(
          event("InputSoloStateChanged",
                {{"inputName", input.title}, {"inputUuid", input.key}, {"inputSolo", input.solo}}));
    if (old->balance != input.balance)
      callback(event(
          "InputBalanceChanged",
          {{"inputName", input.title}, {"inputUuid", input.key}, {"inputBalance", input.balance}}));
    const bool master_routing_changed = routed_to_master(*old) != routed_to_master(input);
    if (master_routing_changed)
      callback(event("InputMasterRoutingChanged",
                     {{"inputName", input.title},
                      {"inputUuid", input.key},
                      {"inputRoutedToMaster", routed_to_master(input)},
                      {"videoActive", active_in_program(replacement, input)}}));
    if (old->volume != input.volume)
      callback(event("InputVolumeChanged",
                     {{"inputName", input.title},
                      {"inputUuid", input.key},
                      {"inputVolumeMul", volume_multiplier(input.volume)},
                      {"inputVolumeDb", volume_db(input.volume)}}));
    if (!master_routing_changed &&
        active_in_program(previous, *old) != active_in_program(replacement, input))
      callback(event("InputActiveStateChanged",
                     {{"inputName", input.title},
                      {"inputUuid", input.key},
                      {"videoActive", active_in_program(replacement, input)}}));
  }
  if ((previous.master_volume != replacement.master_volume ||
       previous.master_muted != replacement.master_muted) &&
      replacement.master_volume)
    callback(event("MasterVolumeChanged",
                   {{"masterVolumeMul", volume_multiplier(*replacement.master_volume)},
                    {"masterVolumeDb", volume_db(*replacement.master_volume)},
                    {"masterMuted", replacement.master_muted}}));
  json meters = json::array();
  for (const auto& input : replacement.inputs)
  {
    if (!input.audio_capable)
      continue;
    const auto channel = [](double level) { return json::array({level, level, level}); };
    meters.push_back(
        {{"inputName", input.title},
         {"inputUuid", input.key},
         {"inputLevelsMul", json::array({channel(input.meter_left), channel(input.meter_right)})}});
  }
  if (!meters.empty())
    callback(event("InputVolumeMeters", {{"inputs", std::move(meters)}}));
  if (previous.streaming != replacement.streaming)
    callback(event("StreamStateChanged",
                   {{"outputActive", replacement.streaming},
                    {"outputState", replacement.streaming ? "started" : "stopped"}}));
  if (previous.streaming_destinations != replacement.streaming_destinations ||
      previous.stream_configuration_known != replacement.stream_configuration_known ||
      previous.stream_configuration_override != replacement.stream_configuration_override)
    callback(event("StreamingDestinationsChanged",
                   {{"streamingDestinations", streaming_destinations(replacement)},
                    {"configurationKnown", replacement.stream_configuration_known}}));
  if (previous.recording != replacement.recording)
    callback(event("RecordStateChanged",
                   {{"outputActive", replacement.recording},
                    {"outputState", replacement.recording ? "recording" : "stopped"}}));
}

void VmixAdapter::trigger_hotkey(const ProductionHotkeyTrigger& trigger)
{
  if (trigger.id.empty() && (trigger.keys.size() != 1 || is_hotkey_modifier(trigger.keys.front())))
  {
    throw HotkeyExecutionError("production-hotkey-chord-unsupported",
                               "vMix KeyPress accepts one shortcut key");
  }
  if (!connected())
    throw std::runtime_error("vMix is not connected");
  std::lock_guard lock(transport_mutex_);
  if (!trigger.id.empty())
    transport_->trigger_shortcut(trigger.id);
  else
    transport_->call("KeyPress", {{"Value", hotkey_key_name(trigger.keys.front())}});
}

std::optional<std::vector<ProductionHotkeyDefinition>> VmixAdapter::hotkeys()
{
  if (!connected())
    return std::nullopt;
  std::lock_guard lock(transport_mutex_);
  return parse_shortcuts(transport_->read_shortcuts());
}

json VmixAdapter::request(std::string_view type,
                          json data,
                          std::source_location caller,
                          std::string_view reason)
{
  static_cast<void>(caller);
  static_cast<void>(reason);
  State snapshot;
  {
    std::lock_guard lock(mutex_);
    if (!connected_)
      throw std::runtime_error("vMix is not connected");
    snapshot = state_;
  }

  if (type.starts_with("Get"))
    return read_request(type, data, snapshot);

  mutation(type, data, snapshot);
  if (type == "SetSceneItemEnabled")
    publish_layer_visibility_confirmation(data, std::move(snapshot));
  else if (type == "StartStream" || type == "StopStream" || type == "StartStreamDestination" ||
           type == "StopStreamDestination")
    publish_streaming_mutation(type, std::move(snapshot));
  return json::object();
}

json VmixAdapter::request_current(std::string_view type,
                                  json data,
                                  std::source_location caller,
                                  std::string_view reason)
{
  static_cast<void>(caller);
  static_cast<void>(reason);
  if (!type.starts_with("Get"))
    throw std::invalid_argument("vMix current-state request must be observational");
  if (!connected())
    throw std::runtime_error("vMix is not connected");

  auto current = refresh();
  {
    std::lock_guard lock(mutex_);
    apply_layer_visibility_confirmations(current, true);
    apply_streaming_destination_activity(current, true);
  }
  const auto result = read_request(type, data, current);
  publish(std::move(current), false);
  return result;
}

void VmixAdapter::publish_layer_visibility_confirmation(const json& data, State replacement)
{
  const auto& selected = selected_scene(replacement, data);
  const auto layer_index = data.at("sceneItemId").get<std::int64_t>();
  const bool visible = data.at("sceneItemEnabled").get<bool>();
  auto composition = std::find_if(replacement.inputs.begin(),
                                  replacement.inputs.end(),
                                  [&](const Input& input) { return input.key == selected.key; });
  auto layer = std::find_if(composition->layers.begin(),
                            composition->layers.end(),
                            [&](const Layer& candidate) { return candidate.index == layer_index; });
  layer->visible = visible;
  layer->visibility_known = true;

  {
    std::lock_guard lock(mutex_);
    auto confirmation = std::find_if(layer_visibility_confirmations_.begin(),
                                     layer_visibility_confirmations_.end(),
                                     [&](const LayerVisibilityConfirmation& candidate)
                                     {
                                       return candidate.composition_key == composition->key &&
                                              candidate.layer_index == layer->index;
                                     });
    if (confirmation == layer_visibility_confirmations_.end())
      layer_visibility_confirmations_.push_back({composition->key, layer->index, visible});
    else
      confirmation->visible = visible;
  }
  publish(std::move(replacement), false);
}

void VmixAdapter::apply_layer_visibility_confirmations(State& replacement,
                                                       bool provider_observation)
{
  auto confirmation = layer_visibility_confirmations_.begin();
  while (confirmation != layer_visibility_confirmations_.end())
  {
    auto composition = std::find_if(replacement.inputs.begin(),
                                    replacement.inputs.end(),
                                    [&](const Input& input)
                                    { return input.key == confirmation->composition_key; });
    if (composition == replacement.inputs.end())
    {
      confirmation = layer_visibility_confirmations_.erase(confirmation);
      continue;
    }
    auto layer = std::find_if(composition->layers.begin(),
                              composition->layers.end(),
                              [&](const Layer& candidate)
                              { return candidate.index == confirmation->layer_index; });
    if (layer == composition->layers.end())
    {
      confirmation = layer_visibility_confirmations_.erase(confirmation);
      continue;
    }
    if (provider_observation && layer->visibility_known && layer->visible == confirmation->visible)
    {
      confirmation = layer_visibility_confirmations_.erase(confirmation);
      continue;
    }
    layer->visible = confirmation->visible;
    layer->visibility_known = true;
    ++confirmation;
  }
}

void VmixAdapter::apply_streaming_destination_activity(State& replacement,
                                                       bool provider_observation)
{
  if (!provider_observation || replacement.streaming_destinations.empty())
    return;

  if (pending_streaming_aggregate_)
  {
    if (replacement.streaming == *pending_streaming_aggregate_)
      pending_streaming_aggregate_.reset();
    else
      replacement.streaming = state_.streaming;
  }

  if (!replacement.streaming)
  {
    for (auto& destination : replacement.streaming_destinations)
    {
      destination.active = false;
      destination.activity_known = true;
      streaming_destination_activity_[destination.stream] = false;
    }
    return;
  }

  if (!state_.streaming && !pending_streaming_aggregate_)
    streaming_destination_activity_.clear();

  for (auto& destination : replacement.streaming_destinations)
  {
    if (destination.activity_known)
    {
      streaming_destination_activity_[destination.stream] = destination.active;
      continue;
    }
    const auto observed = streaming_destination_activity_.find(destination.stream);
    if (observed == streaming_destination_activity_.end())
      continue;
    destination.active = observed->second;
    destination.activity_known = true;
  }
}

void VmixAdapter::record_streaming_mutation(std::string_view type,
                                            const json& data,
                                            const State& state)
{
  std::lock_guard lock(mutex_);
  if (type == "StartStream" || type == "StopStream")
  {
    const bool active = type == "StartStream";
    for (const auto& destination : state.streaming_destinations)
      streaming_destination_activity_[destination.stream] = active;
    pending_streaming_aggregate_ = active;
    return;
  }

  const auto stream = stream_number_from_destination_id(data);
  const bool active = type == "StartStreamDestination";
  streaming_destination_activity_[stream] = active;
  pending_streaming_aggregate_ =
      active || std::ranges::any_of(streaming_destination_activity_,
                                    [](const auto& entry) { return entry.second; });
}

void VmixAdapter::publish_streaming_mutation(std::string_view type, State replacement)
{
  {
    std::lock_guard lock(mutex_);
    for (auto& destination : replacement.streaming_destinations)
    {
      const auto activity = streaming_destination_activity_.find(destination.stream);
      if (activity == streaming_destination_activity_.end())
        continue;
      destination.active = activity->second;
      destination.activity_known = true;
    }
    if (type == "StartStream")
      replacement.streaming = true;
    else if (type == "StopStream")
      replacement.streaming = false;
    else
      replacement.streaming =
          std::ranges::any_of(replacement.streaming_destinations,
                              [](const StreamingDestination& destination)
                              { return destination.activity_known && destination.active; });
  }
  publish(std::move(replacement), false);
}

json VmixAdapter::read_request(std::string_view type, const json& data, const State& state) const
{
  if (type == "GetVersion")
    return {{"obsVersion", state.version}};
  if (type == "GetStudioModeEnabled")
    return {{"studioModeEnabled", true}};
  if (type == "GetSceneList")
  {
    json scenes = json::array();
    for (const auto& input : state.inputs)
      scenes.push_back(
          {{"sceneName", input.title}, {"sceneUuid", input.key}, {"sceneIndex", input.number - 1}});
    const auto* program = find_input(state, state.active_input);
    const auto* preview = find_input(state, state.preview_input);
    return {{"currentProgramSceneName", program ? program->title : ""},
            {"currentProgramSceneUuid", program ? program->key : ""},
            {"currentPreviewSceneName", preview ? preview->title : ""},
            {"currentPreviewSceneUuid", preview ? preview->key : ""},
            {"scenes", std::move(scenes)}};
  }
  if (type == "GetCurrentProgramScene")
  {
    const auto* program = find_input(state, state.active_input);
    return {{"currentProgramSceneName", program ? program->title : ""},
            {"currentProgramSceneUuid", program ? program->key : ""}};
  }
  if (type == "GetCurrentPreviewScene")
  {
    const auto* preview = find_input(state, state.preview_input);
    return {{"currentPreviewSceneName", preview ? preview->title : ""},
            {"currentPreviewSceneUuid", preview ? preview->key : ""}};
  }
  if (type == "GetSceneItemList")
  {
    const auto& composition = selected_scene(state, data);
    return {{"sceneItems", scene_items(state, composition)}};
  }
  if (type == "GetSceneItemEnabled")
  {
    const auto& composition = selected_scene(state, data);
    const auto item_id = data.at("sceneItemId").get<std::int64_t>();
    const auto items = scene_items(state, composition);
    const auto item = std::find_if(items.begin(),
                                   items.end(),
                                   [&](const json& candidate)
                                   { return candidate.value("sceneItemId", -1LL) == item_id; });
    if (item == items.end())
      throw std::out_of_range("vMix layer was not found");
    return {{"sceneItemEnabled", item->value("sceneItemEnabled", false)}};
  }
  if (type == "GetInputList")
  {
    json inputs = json::array();
    for (const auto& input : state.inputs)
      inputs.push_back({{"inputName", input.title},
                        {"inputUuid", input.key},
                        {"inputKind", input.type},
                        {"audioCapable", input.audio_capable},
                        {"inputRoutedToMaster", routed_to_master(input)}});
    return {{"inputs", std::move(inputs)}};
  }
  if (type == "GetInputMute")
    return {{"inputMuted", selected_input(state, data).muted}};
  if (type == "GetInputSolo")
    return {{"inputSolo", selected_input(state, data).solo}};
  if (type == "GetInputBalance")
    return {{"inputBalance", selected_input(state, data).balance}};
  if (type == "GetInputVolume")
  {
    const auto volume = selected_input(state, data).volume;
    return {{"inputVolumeMul", volume_multiplier(volume)}, {"inputVolumeDb", volume_db(volume)}};
  }
  if (type == "GetInputAudioMonitorType")
    throw std::runtime_error("vMix does not expose an equivalent monitoring mode state");
  if (type == "GetMasterVolume" || type == "GetMasterMute")
  {
    if (!state.master_volume)
      throw std::runtime_error("vMix did not expose Master volume state");
    return {{"masterVolumeMul", volume_multiplier(*state.master_volume)},
            {"masterVolumeDb", volume_db(*state.master_volume)},
            {"masterMuted", state.master_muted}};
  }
  if (type == "GetInputActiveState")
    return {{"videoActive", active_in_program(state, selected_input(state, data))}};
  if (type == "GetStreamStatus")
    return {{"outputActive", state.streaming}};
  if (type == "GetRecordStatus")
    return {{"outputActive", state.recording},
            {"outputPaused", false},
            {"outputDuration", 0},
            {"outputTimecode", "00:00:00.000"}};
  if (type == "GetStreamServiceSettings")
  {
    const bool configured = state.stream_configuration_override ||
                            std::ranges::any_of(state.streaming_destinations,
                                                [](const auto& target) { return target.enabled; });
    const auto host = state.streaming_destinations.empty()
                          ? std::string("configured")
                          : state.streaming_destinations.front().host;
    return {{"configurationKnown", state.stream_configuration_known},
            {"streamServiceSettings",
             configured ? json{{"server", host.empty() ? "configured" : host}} : json::object()}};
  }
  if (type == "GetStreamingDestinations")
    return {{"streamingDestinations", streaming_destinations(state)}};
  if (type == "GetSceneTransitionList")
  {
    json transitions = json::array({{{"transitionName", "Cut"},
                                     {"transitionUuid", built_in_cut_transition_id},
                                     {"transitionKind", "Cut"},
                                     {"transitionDuration", 0},
                                     {"transitionFixed", true},
                                     {"transitionConfigurable", false}}});
    for (const auto& transition : state.transitions)
      transitions.push_back(
          {{"transitionName", transition.effect},
           {"transitionUuid", "vmix-transition-" + std::to_string(transition.number)},
           {"transitionKind", transition.effect},
           {"transitionDuration", transition.duration_ms},
           {"transitionFixed", false},
           {"transitionConfigurable", true}});
    return {{"transitions", std::move(transitions)}};
  }
  if (type == "GetCurrentSceneTransition")
  {
    if (state.transitions.empty())
      return json::object();
    const auto& transition = state.transitions.front();
    return {{"transitionName", transition.effect},
            {"transitionUuid", "vmix-transition-" + std::to_string(transition.number)},
            {"transitionKind", transition.effect},
            {"transitionFixed", false},
            {"transitionConfigurable", true},
            {"transitionDuration", transition.duration_ms}};
  }
  throw std::runtime_error("Unsupported vMix read request: " + std::string(type));
}

void VmixAdapter::set_master_mute(bool muted)
{
  std::lock_guard lock(transport_mutex_);
  const auto current = parse_state(transport_->read_state());
  if (!current.master_volume)
    throw std::runtime_error("vMix did not expose Master volume state");
  const auto target = master_mute_.request(muted, volume_db(*current.master_volume));
  if (target)
    transport_->call("SetMasterVolume",
                     {{"Value", std::to_string(std::lround(fader_percentage(*target)))}});
}

void VmixAdapter::mutation(std::string_view type, const json& data, const State& state)
{
  std::string function;
  std::map<std::string, std::string> parameters;
  if (type == "SetCurrentPreviewScene" || type == "SetCurrentProgramScene")
  {
    const auto& input = selected_scene(state, data);
    function = type == "SetCurrentPreviewScene" ? "PreviewInput" : "CutDirect";
    parameters["Input"] = input.key;
  }
  else if (type == "TriggerStudioModeTransition")
  {
    const auto transition_uuid =
        data.is_object() ? data.value("transitionUuid", std::string{}) : std::string{};
    if (transition_uuid.empty())
    {
      function = "QuickPlay";
    }
    else if (transition_uuid == built_in_cut_transition_id)
    {
      function = "Cut";
    }
    else
    {
      const auto selected = std::find_if(
          state.transitions.begin(),
          state.transitions.end(),
          [&](const Transition& transition)
          { return transition_uuid == "vmix-transition-" + std::to_string(transition.number); });
      if (selected == state.transitions.end())
        throw std::out_of_range("vMix transition was not found");
      function = "Transition" + std::to_string(selected->number);
    }
  }
  else if (type == "SetCurrentSceneTransition")
  {
    const auto transition_uuid = required_string(data, "transitionUuid");
    const auto selected = std::find_if(
        state.transitions.begin(),
        state.transitions.end(),
        [&](const Transition& transition)
        { return transition_uuid == "vmix-transition-" + std::to_string(transition.number); });
    if (selected == state.transitions.end())
      throw std::out_of_range("vMix transition was not found");
    function = "SetTransitionEffect" + std::to_string(selected->number);
    parameters["Value"] = required_string(data, "transitionName");
  }
  else if (type == "SetCurrentSceneTransitionDuration" || type == "SetQuickTransitionDuration")
  {
    const auto transition_uuid = required_string(data, "transitionUuid");
    const auto selected = std::find_if(
        state.transitions.begin(),
        state.transitions.end(),
        [&](const Transition& transition)
        { return transition_uuid == "vmix-transition-" + std::to_string(transition.number); });
    if (selected == state.transitions.end())
      throw std::out_of_range("vMix transition was not found");
    function = "SetTransitionDuration" + std::to_string(selected->number);
    parameters["Value"] = std::to_string(data.at("transitionDuration").get<std::uint32_t>());
  }
  else if (type == "SetInputMute")
  {
    const auto& input = selected_input(state, data);
    function = data.at("inputMuted").get<bool>() ? "AudioOff" : "AudioOn";
    parameters["Input"] = input.key;
  }
  else if (type == "SetInputSolo")
  {
    const auto& input = selected_input(state, data);
    const bool solo = data.at("inputSolo").get<bool>();
    if (solo)
    {
      std::lock_guard lock(transport_mutex_);
      for (const auto& candidate : state.inputs)
      {
        if (candidate.key != input.key && candidate.solo)
          transport_->call("SoloOff", {{"Input", candidate.key}});
      }
      transport_->call("SoloOn", {{"Input", input.key}});
      return;
    }
    function = "SoloOff";
    parameters["Input"] = input.key;
  }
  else if (type == "SetInputBalance")
  {
    const auto& input = selected_input(state, data);
    function = "SetBalance";
    parameters["Input"] = input.key;
    parameters["Value"] = std::to_string(data.at("inputBalance").get<double>());
  }
  else if (type == "SetInputVolume")
  {
    const auto& input = selected_input(state, data);
    function = "SetVolume";
    parameters["Input"] = input.key;
    const auto percentage = fader_percentage(data.at("inputVolumeDb").get<double>());
    parameters["Value"] = std::to_string(static_cast<long long>(std::lround(percentage)));
  }
  else if (type == "SetMasterMute")
  {
    set_master_mute(data.at("masterMuted").get<bool>());
    return;
  }
  else if (type == "SetMasterVolume")
  {
    function = "SetMasterVolume";
    const auto percentage = fader_percentage(data.at("masterVolumeDb").get<double>());
    parameters["Value"] = std::to_string(static_cast<long long>(std::lround(percentage)));
  }
  else if (type == "StartStream")
    function = "StartStreaming";
  else if (type == "StopStream")
    function = "StopStreaming";
  else if (type == "StartStreamDestination" || type == "StopStreamDestination")
  {
    function = type == "StartStreamDestination" ? "StartStreaming" : "StopStreaming";
    parameters["Value"] = std::to_string(stream_number_from_destination_id(data));
  }
  else if (type == "StartRecord")
    function = "StartRecording";
  else if (type == "StopRecord")
    function = "StopRecording";
  else if (type == "SetSceneItemEnabled")
  {
    const auto& composition = selected_scene(state, data);
    const auto layer_index = data.at("sceneItemId").get<std::int64_t>();
    if (layer_index <= 0)
      throw std::invalid_argument("vMix base layer visibility is not controllable");
    const auto layer =
        std::find_if(composition.layers.begin(),
                     composition.layers.end(),
                     [&](const Layer& candidate) { return candidate.index == layer_index; });
    if (layer == composition.layers.end())
      throw std::out_of_range("vMix layer was not found");
    function = data.at("sceneItemEnabled").get<bool>() ? "LayerOn" : "LayerOff";
    parameters["Input"] = composition.key;
    parameters["Value"] = std::to_string(layer_index);
  }
  else if (type == "SetInputAudioMonitorType" || type == "PauseRecord" || type == "ResumeRecord")
    throw std::runtime_error("The requested operation has no authoritative vMix API equivalent");
  else
    throw std::runtime_error("Unsupported vMix mutation request: " + std::string(type));
  std::lock_guard lock(transport_mutex_);
  if (type == "SetMasterVolume")
    master_mute_.volume_requested(data.at("masterVolumeDb").get<double>());
  transport_->call(function, parameters);
  if (type == "StartStream" || type == "StopStream" || type == "StartStreamDestination" ||
      type == "StopStreamDestination")
    record_streaming_mutation(type, data, state);
}

} // namespace studio_remote::vmix
