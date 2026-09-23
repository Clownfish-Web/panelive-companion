#include <studio_remote/state/source_membership.hpp>

#include <set>
#include <utility>

namespace studio_remote
{
namespace
{
bool update_occurrences(std::vector<SourceOccurrence>& occurrences,
                        std::string_view container_id,
                        std::int64_t item_id,
                        bool enabled)
{
  bool found = false;
  for (auto& occurrence : occurrences)
  {
    const bool container_matches = !container_id.empty() && occurrence.container_id == container_id;
    if (container_matches && occurrence.item_id == item_id)
    {
      occurrence.enabled = enabled;
      found = true;
    }
    found = update_occurrences(occurrence.children, container_id, item_id, enabled) || found;
  }
  return found;
}

class MembershipTraversal
{
public:
  explicit MembershipTraversal(
      const std::map<std::string, std::vector<SourceOccurrence>, std::less<>>& compositions)
      : compositions_(compositions)
  {
  }

  void composition(std::string_view id, bool enabled, std::size_t depth)
  {
    const auto found = compositions_.find(id);
    if (found == compositions_.end() || depth >= 64 || !path_.insert(std::string(id)).second)
    {
      result.available = false;
      return;
    }
    occurrences(found->second, enabled, depth);
    path_.erase(std::string(id));
  }

  SourceMembershipResult result;

private:
  void occurrences(const std::vector<SourceOccurrence>& values, bool enabled, std::size_t depth)
  {
    if (depth >= 64)
    {
      result.available = false;
      return;
    }
    for (const auto& value : values)
    {
      if (++visited_ > 100'000)
      {
        result.available = false;
        return;
      }
      result.available = result.available && value.available && !value.resource_id.empty();
      const bool active = enabled && value.enabled;
      const auto [entry, inserted] =
          positions_.try_emplace(value.resource_id, result.resources.size());
      if (inserted)
        result.resources.push_back({value.resource_id, value.name, active});
      else
        result.resources[entry->second].active = result.resources[entry->second].active || active;
      occurrences(value.children, active, depth + 1);
      if (!value.referenced_composition_id.empty())
        composition(value.referenced_composition_id, active, depth + 1);
    }
  }

  const std::map<std::string, std::vector<SourceOccurrence>, std::less<>>& compositions_;
  std::map<std::string, std::size_t> positions_;
  std::set<std::string> path_;
  std::size_t visited_ = 0;
};
} // namespace

void SourceMembership::clear()
{
  compositions_.clear();
}

void SourceMembership::replace(std::string composition_id,
                               std::vector<SourceOccurrence> occurrences)
{
  compositions_.insert_or_assign(std::move(composition_id), std::move(occurrences));
}

bool SourceMembership::set_enabled(std::string_view container_id,
                                   std::int64_t item_id,
                                   bool enabled)
{
  bool found = false;
  for (auto& [id, occurrences] : compositions_)
  {
    static_cast<void>(id);
    found = update_occurrences(occurrences, container_id, item_id, enabled) || found;
  }
  return found;
}

SourceMembershipResult SourceMembership::resolve(std::string_view composition_id) const
{
  MembershipTraversal traversal(compositions_);
  traversal.composition(composition_id, true, 0);
  return std::move(traversal.result);
}
} // namespace studio_remote
