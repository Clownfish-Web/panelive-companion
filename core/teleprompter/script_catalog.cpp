#include <studio_remote/teleprompter/script_catalog.hpp>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace studio_remote::teleprompter
{
namespace
{
template <typename Value> Result<Value> missing()
{
  return {{}, Error::not_found, "Teleprompter script was not found"};
}

Script with_delivery(const LoadedScript& loaded, Delivery delivery)
{
  auto script = loaded.script;
  script.delivery = std::move(delivery);
  return script;
}
} // namespace

ScriptCatalogView ScriptCatalog::snapshot() const
{
  std::lock_guard lock(mutex_);
  return scripts_;
}

Result<std::vector<Summary>> ScriptCatalog::library() const
{
  std::vector<Summary> result;
  const auto scripts = snapshot();
  for (const auto& [id, loaded] : *scripts)
  {
    static_cast<void>(id);
    result.push_back(loaded->script.summary);
  }
  std::ranges::sort(result,
                    [](const Summary& left, const Summary& right)
                    {
                      if (left.modified_at_unix_ms != right.modified_at_unix_ms)
                        return left.modified_at_unix_ms > right.modified_at_unix_ms;
                      return left.id < right.id;
                    });
  return {std::move(result), Error::none, {}};
}

Result<Script> ScriptCatalog::library_script(std::string_view script_id) const
{
  const auto scripts = snapshot();
  const auto found = scripts->find(script_id);
  return found == scripts->end() ? missing<Script>()
                                 : Result<Script>{found->second->script, Error::none, {}};
}

std::shared_ptr<const LoadedScript> ScriptCatalog::find_locked(const SessionDevice& device,
                                                               std::string_view script_id) const
{
  const auto found = scripts_->find(script_id);
  if (found == scripts_->end() || !found->second->assigned_to(device))
    return {};
  return found->second;
}

Result<std::vector<Summary>> ScriptCatalog::list(const SessionDevice& device) const
{
  std::vector<Summary> result;
  {
    std::lock_guard lock(mutex_);
    for (const auto& [id, loaded] : *scripts_)
    {
      static_cast<void>(id);
      if (loaded->assigned_to(device))
        result.push_back(loaded->script.summary);
    }
  }
  std::ranges::sort(result,
                    [](const Summary& left, const Summary& right)
                    {
                      if (left.modified_at_unix_ms != right.modified_at_unix_ms)
                        return left.modified_at_unix_ms > right.modified_at_unix_ms;
                      return left.id < right.id;
                    });
  return {std::move(result), Error::none, {}};
}

Result<Script> ScriptCatalog::get(const SessionDevice& device, std::string_view script_id) const
{
  std::shared_ptr<const LoadedScript> loaded;
  Delivery delivery;
  {
    std::lock_guard lock(mutex_);
    loaded = find_locked(device, script_id);
    if (!loaded)
      return missing<Script>();
    delivery = deliveries_.at(loaded->script.summary.id).at(device.id);
  }
  return {with_delivery(*loaded, std::move(delivery)), Error::none, {}};
}

Result<Script>
ScriptCatalog::retrieve(const SessionDevice& device, std::string_view script_id, std::uint64_t now)
{
  if (now > maximum_safe_integer)
    return {
        {}, Error::storage_unavailable, "Teleprompter storage is unavailable; retry the operation"};
  std::shared_ptr<const LoadedScript> loaded;
  Delivery result;
  {
    std::lock_guard lock(mutex_);
    loaded = find_locked(device, script_id);
    if (!loaded)
      return missing<Script>();
    auto& delivery = deliveries_.at(loaded->script.summary.id).at(device.id);
    delivery.retrieved_revision = loaded->script.summary.revision;
    delivery.retrieved_at_unix_ms = std::max(now, delivery.retrieved_at_unix_ms.value_or(0));
    result = delivery;
  }
  return {with_delivery(*loaded, std::move(result)), Error::none, {}};
}

Result<Delivery> ScriptCatalog::report_progress(const SessionDevice& device,
                                                std::string_view script_id,
                                                const Progress& progress,
                                                std::uint64_t now)
{
  std::lock_guard lock(mutex_);
  const auto loaded = find_locked(device, script_id);
  if (!loaded)
    return missing<Delivery>();
  auto& delivery = deliveries_.at(loaded->script.summary.id).at(device.id);
  auto accepted = accept_progress(loaded->script, delivery, progress, now);
  if (accepted)
    delivery = *accepted.value;
  return accepted;
}

bool ScriptCatalog::capacity_available(std::size_t previous_size,
                                       std::size_t new_size,
                                       bool creating) const
{
  std::lock_guard lock(mutex_);
  if (previous_size > library_bytes_ || library_bytes_ - previous_size > maximum_library_bytes ||
      new_size > maximum_library_bytes - (library_bytes_ - previous_size))
    return false;
  if (!creating)
    return true;
  return scripts_->size() < maximum_library_scripts;
}

ScriptCatalog::Publication ScriptCatalog::prepare(std::shared_ptr<const LoadedScript> script) const
{
  if (!script)
    throw std::invalid_argument("A compiled authored script is required");
  Publication publication;
  publication.id = script->script.summary.id;
  auto index = std::make_shared<ScriptCatalogSnapshot>(*snapshot());
  index->insert_or_assign(publication.id, script);
  publication.index = std::move(index);
  decltype(deliveries_) delivery;
  auto& assigned = delivery[publication.id];
  for (const auto& [device, generation] : script->assignments)
  {
    static_cast<void>(generation);
    assigned.emplace(device, Delivery{});
  }
  publication.delivery = delivery.extract(publication.id);
  return publication;
}

ScriptCatalog::Publication ScriptCatalog::prepare_erase(std::string_view script_id) const
{
  Publication publication;
  publication.id = script_id;
  auto index = std::make_shared<ScriptCatalogSnapshot>(*snapshot());
  index->erase(publication.id);
  publication.index = std::move(index);
  return publication;
}

void ScriptCatalog::publish(Publication publication)
{
  std::lock_guard lock(mutex_);
  const auto previous = scripts_->find(publication.id);
  const auto replacement = publication.index->find(publication.id);
  if (previous != scripts_->end())
    library_bytes_ -= previous->second->script.document.draft.content.size();
  if (replacement == publication.index->end())
    deliveries_.erase(publication.id);
  else
  {
    library_bytes_ += replacement->second->script.document.draft.content.size();
    auto& prepared = publication.delivery.mapped();
    if (previous != scripts_->end())
      for (auto& [device, delivery] : prepared)
      {
        const auto old = previous->second->assignments.find(device);
        if (old == previous->second->assignments.end() ||
            old->second != replacement->second->assignments.at(device))
          continue;
        auto& prior = deliveries_.at(publication.id).at(device);
        if (previous->second->script.summary.revision ==
            replacement->second->script.summary.revision)
          delivery = std::move(prior);
        else
        {
          delivery.retrieved_revision = prior.retrieved_revision;
          delivery.retrieved_at_unix_ms = prior.retrieved_at_unix_ms;
        }
      }
    deliveries_.erase(publication.id);
    deliveries_.insert(std::move(publication.delivery));
  }
  scripts_.swap(publication.index);
}
} // namespace studio_remote::teleprompter
