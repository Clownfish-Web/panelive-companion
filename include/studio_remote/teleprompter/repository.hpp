#pragma once

#include <studio_remote/teleprompter/script.hpp>
#include <studio_remote/teleprompter/session_repository.hpp>

namespace studio_remote::teleprompter
{
/// Thread-safe authored library and explicit client attachments. Library CRUD and attach/detach
/// are trusted local management operations. Device-scoped reads/progress recheck active pairing
/// and attachment; mobile requests never choose another principal or mutate the library.
/// Implementations return committed outcomes; no failure may publish a partial authored record.
class IRepository
{
public:
  virtual ~IRepository() = default;
  virtual ISessionRepository* shared_sessions() noexcept = 0;
  virtual Result<std::vector<Summary>> library() = 0;
  virtual Result<Script> library_script(std::string_view script_id) = 0;
  virtual Result<bool> attach(std::string_view device_id, std::string_view script_id) = 0;
  virtual Result<bool> detach(std::string_view device_id, std::string_view script_id) = 0;
  virtual Result<std::vector<Summary>> list(std::string_view device_id) = 0;
  virtual Result<Script> get(std::string_view device_id, std::string_view script_id) = 0;
  virtual Result<Script> retrieve(std::string_view device_id, std::string_view script_id) = 0;
  virtual Result<Script> create(Draft draft) = 0;
  virtual Result<Script>
  update(std::string_view script_id, std::uint64_t expected_revision, Draft draft) = 0;
  virtual Result<bool> erase(std::string_view script_id, std::uint64_t expected_revision) = 0;
  virtual Result<Delivery> report_progress(std::string_view device_id,
                                           std::string_view script_id,
                                           const Progress& progress) = 0;
};
} // namespace studio_remote::teleprompter
