#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <studio_remote/common/types.hpp>
#include <studio_remote/streamlabs/streamlabs_state.hpp>

namespace studio_remote::streamlabs
{

inline constexpr std::size_t maximum_message_bytes = 1U << 20;
inline constexpr std::size_t maximum_catalog_entries = 512;
inline constexpr std::size_t maximum_nodes_per_scene = 2'048;
inline constexpr std::size_t maximum_string_bytes = 4'096;

struct RpcMessage
{
  std::optional<std::uint64_t> id;
  json result;
  std::string event_resource;
  json event_data;
  bool event = false;
  bool promise_event = false;
  bool promise_rejected = false;
};

enum class PromiseEventDisposition
{
  unrelated,
  resolved,
  rejected
};

json make_request(std::uint64_t id,
                  std::string_view resource,
                  std::string_view method,
                  json arguments = json::array());
RpcMessage parse_message(std::string_view message);
std::optional<std::string> promise_subscription_id(const json& value);
PromiseEventDisposition classify_promise_event(const RpcMessage& message,
                                               std::string_view expected_resource) noexcept;
SceneCollection parse_active_collection(const json& value);
std::vector<Scene> parse_scenes(const json& value);
std::string parse_active_scene_id(const json& value);
bool parse_studio_mode(const json& value);
PerformanceState parse_performance_state(const json& value);
std::vector<AudioSource> parse_audio_sources(const json& value);
AudioSource parse_audio_source(const json& value, std::string_view resource_id);
bool parse_scene_item_visibility(const json& value);
OutputState parse_output_state(std::string_view value) noexcept;
std::pair<OutputState, OutputState> parse_output_states(const json& value);
double volume_db_to_deflection(double volume_db) noexcept;
std::string sanitize_protocol_error(std::string_view value);
bool allowed_resource(std::string_view resource) noexcept;
bool allowed_method(std::string_view resource, std::string_view method) noexcept;
std::span<const std::string_view> event_allowlist() noexcept;

} // namespace studio_remote::streamlabs
