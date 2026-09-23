#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{
// An occurrence retains its native container solely for authoritative visibility events.
// This graph is the recursive membership projection, not a source-control target catalog.
struct SourceOccurrence
{
  std::string resource_id;
  std::string name;
  std::string container_id;
  std::int64_t item_id = 0;
  bool enabled = false;
  bool available = true;
  std::string referenced_composition_id;
  std::vector<SourceOccurrence> children;
};

struct SourceMembershipEntry
{
  std::string resource_id;
  std::string name;
  bool active = false;
};

struct SourceMembershipResult
{
  bool available = true;
  std::vector<SourceMembershipEntry> resources;
};

// Owned by StateStore; all access is serialized by its state mutex.
class SourceMembership
{
public:
  void clear();
  void replace(std::string composition_id, std::vector<SourceOccurrence> occurrences);
  bool set_enabled(std::string_view container_id, std::int64_t item_id, bool enabled);
  SourceMembershipResult resolve(std::string_view composition_id) const;

private:
  std::map<std::string, std::vector<SourceOccurrence>, std::less<>> compositions_;
};
} // namespace studio_remote
