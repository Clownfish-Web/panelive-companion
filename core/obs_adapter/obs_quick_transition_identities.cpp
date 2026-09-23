#include <studio_remote/obs/obs_quick_transition_identities.hpp>

#include <limits>
#include <studio_remote/obs/obs_adapter.hpp>

namespace studio_remote
{
void ObsQuickTransitionIdentities::replace(json& catalog)
{
  std::lock_guard lock(mutex_);
  backend_names_.clear();
  native_ids_.clear();
  for (auto& transition : catalog)
  {
    const auto transition_id = transition.value("quickTransitionId", "");
    const auto backend_name = transition.value("backendTransitionName", "");
    if (!transition_id.empty() && !backend_name.empty())
      backend_names_[transition_id] = backend_name;
    if (!transition_id.empty() && transition.value("quickTransitionNativeId", 0) > 0)
      native_ids_[transition_id] = transition.value("quickTransitionNativeId", 0);
    transition.erase("backendTransitionName");
    transition.erase("quickTransitionNativeId");
  }
}

ObsQuickTransitionSelection ObsQuickTransitionIdentities::resolve(json& data) const
{
  ObsQuickTransitionSelection selection;
  const auto quick_transition_id = data.find("quickTransitionId");
  if (quick_transition_id == data.end() || !quick_transition_id->is_string())
    return selection;
  const auto transition_id = quick_transition_id->get<std::string>();
  std::lock_guard lock(mutex_);
  if (const auto name = backend_names_.find(transition_id);
      name != backend_names_.end() && data.contains("transitionName"))
    data["transitionName"] = name->second;
  if (const auto native = native_ids_.find(transition_id); native != native_ids_.end())
    selection.native_id = native->second;
  const auto duration = data.find("transitionDuration");
  if (duration != data.end() && duration->is_number_unsigned() &&
      duration->get<std::uint64_t>() <= std::numeric_limits<std::uint32_t>::max())
    selection.duration_ms = duration->get<std::uint32_t>();
  data.erase(quick_transition_id);
  return selection;
}

void prepare_obs_transition_fallback(IObsAdapter& adapter, json& data, std::source_location caller)
{
  const auto duration = data.find("transitionDuration");
  const auto name = data.find("transitionName");
  if (name == data.end() || !name->is_string() || name->get_ref<const std::string&>().empty())
    return;
  adapter.request("SetCurrentSceneTransition",
                  {{"transitionName", *name}},
                  caller,
                  "select requested quick transition");
  if (duration != data.end() && duration->is_number_unsigned())
    adapter.request("SetCurrentSceneTransitionDuration",
                    {{"transitionDuration", *duration}},
                    caller,
                    "apply requested quick transition duration");
  data = json::object();
}
} // namespace studio_remote
