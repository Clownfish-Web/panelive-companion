#include <studio_remote/ui/teleprompter_controller.hpp>

#include <utility>

namespace studio_remote
{
TeleprompterUiController::TeleprompterUiController(teleprompter::IRepository* repository)
    : repository_(repository)
{
}

const TeleprompterUiState& TeleprompterUiController::state() const noexcept
{
  return state_;
}

bool TeleprompterUiController::has_unsaved_changes() const noexcept
{
  return state_.sharing || (state_.editor && state_.editor->draft != state_.editor->original);
}

void TeleprompterUiController::open_library()
{
  if (has_unsaved_changes())
  {
    state_.error = "Save or discard the current changes before opening the library";
    return;
  }
  close();
  state_.view = TeleprompterView::library;
  refresh();
}

void TeleprompterUiController::open(std::string device_id, std::string client_name)
{
  if (has_unsaved_changes())
  {
    state_.error = "Save or discard the current script before opening another client";
    return;
  }
  close();
  state_.view = TeleprompterView::client;
  state_.device_id = std::move(device_id);
  state_.client_name = std::move(client_name);
  refresh();
}

void TeleprompterUiController::close() noexcept
{
  const auto generation = state_.form_generation + 1;
  auto role_choices = std::move(state_.role_choices);
  state_ = {};
  state_.role_choices = std::move(role_choices);
  state_.form_generation = generation;
}

void TeleprompterUiController::refresh()
{
  if (state_.view == TeleprompterView::closed)
    return;
  if (!repository_)
  {
    state_.error = "Teleprompter is unavailable";
    return;
  }
  const auto library = repository_->library();
  if (!library)
  {
    state_.error = library.message;
    return;
  }
  state_.library = *library.value;
  if (state_.view == TeleprompterView::library)
    return;
  const auto result = repository_->list(state_.device_id);
  if (result.error == teleprompter::Error::device_unavailable)
  {
    close();
    return;
  }
  if (!result)
  {
    state_.error = result.message;
    return;
  }
  state_.scripts = *result.value;
  if (auto* sessions = repository_->shared_sessions())
  {
    auto shared = sessions->sessions(state_.device_id);
    if (shared)
      state_.sessions = std::move(*shared.value);
    else
      state_.error = shared.message;
  }
}

void TeleprompterUiController::add_script()
{
  if (state_.view != TeleprompterView::library || !repository_ || state_.editor || state_.sharing)
    return;
  state_.editor.emplace();
  state_.error.clear();
  ++state_.form_generation;
}

void TeleprompterUiController::edit_script(std::string_view script_id)
{
  if (state_.view != TeleprompterView::library || !repository_ || state_.editor || state_.sharing)
    return;
  auto result = repository_->library_script(script_id);
  if (!result)
  {
    state_.error = result.message;
    return;
  }
  const auto& script = *result.value;
  state_.editor = TeleprompterEditor{
      script.summary.id, script.summary.revision, script.document.draft, script.document.draft};
  state_.error.clear();
  ++state_.form_generation;
}

void TeleprompterUiController::set_name(std::string name)
{
  if (state_.editor)
    state_.editor->draft.name = std::move(name);
}

void TeleprompterUiController::set_content(std::string content)
{
  if (state_.editor)
    state_.editor->draft.content = std::move(content);
}

void TeleprompterUiController::set_reading_speed(std::uint32_t words_per_minute)
{
  if (state_.editor)
    state_.editor->draft.words_per_minute = words_per_minute;
}

void TeleprompterUiController::save_script()
{
  if (!state_.editor || !repository_)
    return;
  const auto& editor = *state_.editor;
  auto result = editor.script_id
                    ? repository_->update(*editor.script_id, editor.revision, editor.draft)
                    : repository_->create(editor.draft);
  if (!result)
  {
    state_.error = result.message;
    return;
  }
  cancel_edit();
  refresh();
}

void TeleprompterUiController::cancel_edit() noexcept
{
  state_.editor.reset();
  state_.error.clear();
  ++state_.form_generation;
}

void TeleprompterUiController::delete_script(std::string_view script_id,
                                             std::uint64_t expected_revision)
{
  if (!repository_ || state_.view != TeleprompterView::library)
    return;
  const auto result = repository_->erase(script_id, expected_revision);
  if (!result)
  {
    state_.error = result.message;
    return;
  }
  state_.error.clear();
  refresh();
}

void TeleprompterUiController::attach_script(std::string_view script_id)
{
  if (!repository_ || state_.view != TeleprompterView::client || state_.sharing)
    return;
  const auto result = repository_->attach(state_.device_id, script_id);
  refresh();
  state_.error = result ? std::string{} : result.message;
}

void TeleprompterUiController::detach_script(std::string_view script_id)
{
  if (!repository_ || state_.view != TeleprompterView::client || state_.sharing)
    return;
  const auto result = repository_->detach(state_.device_id, script_id);
  refresh();
  state_.error = result ? std::string{} : result.message;
}

} // namespace studio_remote
