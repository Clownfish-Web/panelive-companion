#pragma once

#include <studio_remote/teleprompter/runtime_resources.hpp>

#include <map>
#include <mutex>

namespace studio_remote::teleprompter
{
/// Process-local immutable authored revisions and separate mutable delivery state.
/// Every method is thread-safe. Callers obtain device/clock snapshots before calling this class;
/// no callback, compilation or persistent I/O runs while its mutex is held.
class ScriptCatalog
{
public:
  // A prepared mutation owns every allocation needed for publication after durable commit.
  // The storage owner serializes prepare/commit/publish; readers remain concurrent.
  class Publication
  {
  public:
    Publication(Publication&&) = default;
    Publication& operator=(Publication&&) = default;

  private:
    friend class ScriptCatalog;
    Publication() = default;
    ScriptCatalogView index;
    std::string id;
    std::map<std::string, std::map<std::string, Delivery, std::less<>>, std::less<>>::node_type
        delivery;
  };

  ScriptCatalogView snapshot() const;
  Result<std::vector<Summary>> library() const;
  Result<Script> library_script(std::string_view script_id) const;
  Result<std::vector<Summary>> list(const SessionDevice& device) const;
  Result<Script> get(const SessionDevice& device, std::string_view script_id) const;
  Result<Script>
  retrieve(const SessionDevice& device, std::string_view script_id, std::uint64_t now);
  Result<Delivery> report_progress(const SessionDevice& device,
                                   std::string_view script_id,
                                   const Progress& progress,
                                   std::uint64_t now);
  bool capacity_available(std::size_t previous_size, std::size_t new_size, bool creating) const;
  Publication prepare(std::shared_ptr<const LoadedScript> script) const;
  Publication prepare_erase(std::string_view script_id) const;
  /// No allocation after commit. An edit retains current retrieval, but clears progress.
  void publish(Publication publication);

private:
  std::shared_ptr<const LoadedScript> find_locked(const SessionDevice& device,
                                                  std::string_view script_id) const;
  mutable std::mutex mutex_;
  ScriptCatalogView scripts_ = std::make_shared<const ScriptCatalogSnapshot>();
  std::map<std::string, std::map<std::string, Delivery, std::less<>>, std::less<>> deliveries_;
  std::size_t library_bytes_ = 0;
};
} // namespace studio_remote::teleprompter
