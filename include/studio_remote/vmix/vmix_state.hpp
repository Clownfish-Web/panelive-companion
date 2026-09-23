#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::vmix
{

struct Layer
{
  std::uint32_t index = 0;
  std::string input_key;
  bool visible = true;
  bool visibility_known = false;
  bool operator==(const Layer&) const = default;
};

struct Input
{
  std::string key;
  std::uint32_t number = 0;
  std::string type;
  std::string title;
  bool audio_capable = false;
  bool muted = false;
  bool solo = false;
  double balance = 0.0;
  double volume = 100.0;
  double meter_left = 0.0;
  double meter_right = 0.0;
  std::string audio_buses;
  std::string color;
  std::vector<Layer> layers;
  bool operator==(const Input&) const = default;
};

struct Transition
{
  std::uint32_t number = 0;
  std::string effect;
  std::uint32_t duration_ms = 1'000;
  bool operator==(const Transition&) const = default;
};

struct StreamingDestination
{
  std::uint32_t stream = 0;
  std::string name_or_key;
  std::string host;
  std::string service_kind = "custom";
  bool configured = false;
  bool enabled = false;
  bool active = false;
  bool activity_known = true;
  bool operator==(const StreamingDestination&) const = default;
};

struct State
{
  std::string version;
  std::vector<Input> inputs;
  std::vector<Transition> transitions;
  std::vector<StreamingDestination> streaming_destinations;
  std::vector<std::uint32_t> overlay_inputs;
  std::uint32_t active_input = 0;
  std::uint32_t preview_input = 0;
  bool recording = false;
  bool streaming = false;
  bool stream_configuration_known = false;
  bool stream_configuration_override = false;
  std::optional<double> master_volume;
  bool master_muted = false; // Companion-owned gain mute, not the native Master Audio toggle.
  bool operator==(const State&) const = default;
};

State parse_state(std::string_view xml);
void apply_local_preset_metadata(State& state, std::string_view preset_xml);
const Input* find_input(const State& state, std::string_view key_or_title);
const Input* find_input(const State& state, std::uint32_t number);

} // namespace studio_remote::vmix
