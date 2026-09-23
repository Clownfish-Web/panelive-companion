#pragma once

#include <studio_remote/teleprompter/repository.hpp>

#include <map>
#include <span>

namespace studio_remote
{
struct ManagementClientRow;

struct TeleprompterEditor
{
  std::optional<std::string> script_id;
  std::uint64_t revision = 0;
  teleprompter::Draft original;
  teleprompter::Draft draft;
};

enum class TeleprompterView
{
  closed,
  library,
  client
};

struct TeleprompterUiState
{
  TeleprompterView view = TeleprompterView::closed;
  std::vector<teleprompter::Summary> library;
  std::string device_id;
  std::string client_name;
  std::vector<teleprompter::Summary> scripts;
  std::optional<TeleprompterEditor> editor;
  std::optional<teleprompter::SessionAssignment> sharing;
  std::vector<teleprompter::SessionSnapshot> sessions;
  std::string error;
  std::map<std::string, std::vector<teleprompter::SessionRole>, std::less<>> role_choices;
  // Identifies a form instance so native refresh can retain its controls and caret.
  std::uint64_t form_generation = 0;
};

/// Confined to the same native UI execution context as ManagementUiController. Storage is
/// borrowed from composition and outlives the frontend; no platform or transport types enter it.
class TeleprompterUiController
{
public:
  explicit TeleprompterUiController(teleprompter::IRepository* repository);
  const TeleprompterUiState& state() const noexcept;
  bool has_unsaved_changes() const noexcept;
  void open_library();
  void open(std::string device_id, std::string client_name);
  void attach_script(std::string_view script_id);
  void detach_script(std::string_view script_id);
  void close() noexcept;
  void refresh();
  void add_script();
  void edit_script(std::string_view script_id);
  void set_name(std::string name);
  void set_content(std::string content);
  void set_reading_speed(std::uint32_t words_per_minute);
  void save_script();
  void cancel_edit() noexcept;
  void delete_script(std::string_view script_id, std::uint64_t expected_revision);
  void update_clients(std::span<const ManagementClientRow> clients);
  std::vector<teleprompter::SessionRole> shared_roles(std::string_view device_id) const;
  teleprompter::SessionRole default_shared_role(std::string_view device_id) const;
  void configure_sharing();
  void select_shared_script(std::string script_id, bool selected);
  void select_shared_client(std::string device_id, bool selected);
  void set_shared_role(std::string_view device_id, teleprompter::SessionRole role);
  void save_sharing();
  void cancel_sharing() noexcept;
  void stop_sharing(std::string_view session_id, std::uint64_t expected_version);

private:
  teleprompter::IRepository* repository_;
  TeleprompterUiState state_;
};
} // namespace studio_remote
