#include <studio_remote/common/client_request_lifetime.hpp>
#include <studio_remote/ui/management_approval_dispatcher.hpp>
#include <studio_remote/ui/management_controller.hpp>

#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace studio_remote
{
void report_management_approval_error(ManagementUiController& controller, std::string_view message)
{
  controller.refresh();
  controller.report_error(std::string(message));
}

class ManagementApprovalDispatcher::State final : public std::enable_shared_from_this<State>
{
public:
  explicit State(ManagementApprovalDispatchActions actions) : actions_(std::move(actions))
  {
  }

  bool request(RoleChangeRequest request)
  {
    Key key{Kind::role, request.device_id, request.request_id};
    return admit(std::move(key), std::move(request));
  }

  bool request(GlobalSettingChangeRequest request)
  {
    Key key{Kind::global, request.device_id, request.request_id};
    return admit(std::move(key), std::move(request));
  }

  void cancel_device(std::string_view device_id)
  {
    auto lease = enter();
    if (!lease)
      return;
    std::vector<std::shared_ptr<Pending>> cancelled;
    {
      std::lock_guard lock(mutex_);
      for (auto entry = pending_.begin(); entry != pending_.end();)
      {
        if (std::get<1>(entry->first) == device_id && !entry->second->executing)
        {
          cancelled.push_back(entry->second);
          entry = pending_.erase(entry);
        }
        else
          ++entry;
      }
    }
    if (!cancelled.empty())
    {
      dismiss(cancelled);
      for (const auto& request : cancelled)
        decide(*request, false, false);
    }
    std::unique_lock lock(mutex_);
    idle_.wait(lock,
               [&]
               {
                 for (const auto& [key, request] : pending_)
                   if (std::get<1>(key) == device_id && request->executing)
                     return false;
                 return true;
               });
  }

  void close()
  {
    std::map<Key, std::shared_ptr<Pending>> pending;
    {
      std::unique_lock lock(mutex_);
      if (!accepting_)
        return;
      accepting_ = false;
      idle_.wait(lock, [this] { return active_callbacks_ == 0; });
      pending = std::move(pending_);
    }
    for (const auto& [key, request] : pending)
    {
      static_cast<void>(key);
      decide(*request, false, false);
    }
    actions_ = {};
  }

  void expire()
  {
    auto lease = enter();
    if (!lease)
      return;
    const auto now = actions_.now();
    std::vector<std::shared_ptr<Pending>> expired;
    {
      std::lock_guard lock(mutex_);
      for (auto entry = pending_.begin(); entry != pending_.end();)
      {
        if (!entry->second->executing &&
            now - entry->second->admitted_at >= maximum_client_request_age)
        {
          expired.push_back(entry->second);
          entry = pending_.erase(entry);
        }
        else
          ++entry;
      }
    }
    if (!expired.empty())
    {
      dismiss(expired);
      for (const auto& request : expired)
        decide(*request, false, false);
    }
  }

  void expire_safely() noexcept
  {
    try
    {
      expire();
    }
    catch (...)
    {
      try
      {
        actions_.report_error("Could not expire client approval requests");
      }
      catch (...)
      {
      }
    }
  }

private:
  enum class Kind
  {
    role,
    global
  };
  using Key = std::tuple<Kind, std::string, std::string>;
  using Request = std::variant<RoleChangeRequest, GlobalSettingChangeRequest>;
  struct Pending
  {
    Key key;
    Request request;
    Clock::time_point admitted_at;
    bool deciding = false;
    bool executing = false;
  };

  /// Native queues may retain inert cancelled tasks. Count their storage until it is released.
  class PresentationSlot
  {
  public:
    explicit PresentationSlot(std::weak_ptr<State> owner) : owner_(std::move(owner))
    {
    }
    PresentationSlot(const PresentationSlot&) = delete;
    PresentationSlot& operator=(const PresentationSlot&) = delete;
    ~PresentationSlot()
    {
      if (auto owner = owner_.lock())
      {
        std::lock_guard lock(owner->mutex_);
        --owner->retained_presentations_;
      }
    }

  private:
    std::weak_ptr<State> owner_;
  };

  // Caller owns mutex_. Each slot is captured by one native task, never by state itself.
  std::shared_ptr<PresentationSlot> presentation_slot()
  {
    auto slot = std::make_shared<PresentationSlot>(weak_from_this());
    ++retained_presentations_;
    return slot;
  }

  /// An executing caller can retain bookkeeping, but close joins it before targets disappear.
  class Lease
  {
  public:
    explicit Lease(State& owner) : owner_(&owner)
    {
    }
    Lease(Lease&& other) noexcept : owner_(std::exchange(other.owner_, nullptr))
    {
    }
    ~Lease()
    {
      if (owner_)
      {
        std::lock_guard lock(owner_->mutex_);
        --owner_->active_callbacks_;
        owner_->idle_.notify_all();
      }
    }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

  private:
    State* owner_;
  };

  std::optional<Lease> enter()
  {
    std::lock_guard lock(mutex_);
    if (!accepting_)
      return std::nullopt;
    ++active_callbacks_;
    return std::optional<Lease>(std::in_place, *this);
  }

  bool current(const std::shared_ptr<Pending>& request) const
  {
    const auto found = pending_.find(request->key);
    return found != pending_.end() && found->second == request;
  }

  void erase(const std::shared_ptr<Pending>& request)
  {
    std::lock_guard lock(mutex_);
    if (current(request))
      pending_.erase(request->key);
    idle_.notify_all();
  }

  void dismiss(const std::vector<std::shared_ptr<Pending>>& cancelled)
  {
    const auto weak = weak_from_this();
    std::shared_ptr<PresentationSlot> slot;
    {
      std::lock_guard lock(mutex_);
      slot = presentation_slot();
    }
    try
    {
      if (actions_.post_presentation(
              [weak, cancelled, slot]
              {
                static_cast<void>(slot);
                if (auto state = weak.lock())
                {
                  auto lease = state->enter();
                  if (!lease)
                    return;
                  for (const auto& request : cancelled)
                    state->dismiss(*request);
                }
              }))
        return;
    }
    catch (...)
    {
      // Pending identity is already invalidated even when native dispatch is unavailable.
    }
    actions_.report_error("Could not dismiss cancelled client approvals");
  }

  void dismiss(const Pending& pending)
  {
    try
    {
      const auto& action = std::holds_alternative<RoleChangeRequest>(pending.request)
                               ? actions_.cancel_role
                               : actions_.cancel_global;
      if (action)
        action(std::get<1>(pending.key), std::get<2>(pending.key));
    }
    catch (...)
    {
      actions_.report_error("Could not dismiss cancelled client approvals");
    }
  }

  bool admit(Key key, Request request)
  {
    auto lease = enter();
    if (!lease)
      return false;
    auto pending =
        std::make_shared<Pending>(Pending{std::move(key), std::move(request), actions_.now()});
    std::shared_ptr<PresentationSlot> slot;
    {
      std::lock_guard lock(mutex_);
      if (pending_.contains(pending->key))
        return true;
      if (pending_.size() >= maximum_pending_requests ||
          retained_presentations_ >= maximum_pending_requests)
        return false;
      slot = presentation_slot();
      pending_.emplace(pending->key, pending);
    }
    const auto weak = weak_from_this();
    try
    {
      if (actions_.post_presentation(
              [weak, pending, slot]
              {
                static_cast<void>(slot);
                if (auto state = weak.lock())
                  state->present(pending);
              }))
        return true;
    }
    catch (...)
    {
      erase(pending);
      throw;
    }
    erase(pending);
    return false;
  }

  void present(const std::shared_ptr<Pending>& pending)
  {
    expire();
    auto lease = enter();
    if (!lease)
      return;
    {
      std::lock_guard lock(mutex_);
      if (!current(pending) || pending->deciding)
        return;
    }
    const auto weak = weak_from_this();
    ManagementApprovalDecision completion = [weak, pending](bool approved)
    {
      if (auto state = weak.lock())
        state->respond(pending, approved, false);
    };
    bool presented = false;
    try
    {
      if (const auto* request = std::get_if<RoleChangeRequest>(&pending->request))
        presented = actions_.present_role({request->request_id,
                                           request->device_id,
                                           request->device_name,
                                           std::string(role_name(request->requested_role)),
                                           request->production_backend_name},
                                          std::move(completion));
      else
      {
        const auto& setting = std::get<GlobalSettingChangeRequest>(pending->request);
        presented = actions_.present_global(
            {setting.request_id,
             setting.device_id,
             setting.device_name,
             setting.setting_id,
             setting.setting_name,
             setting.warning,
             setting.requested_value.is_boolean() && setting.requested_value.get<bool>()},
            std::move(completion));
      }
    }
    catch (...)
    {
      // A native presentation failure is a rejected decision, not an abandoned pending request.
    }
    if (!presented)
      respond(pending, false, true);
  }

  void respond(const std::shared_ptr<Pending>& pending, bool approved, bool presentation_failed)
  {
    auto lease = enter();
    if (!lease)
      return;
    {
      std::lock_guard lock(mutex_);
      if (!current(pending) || pending->deciding)
        return;
      pending->deciding = true;
    }
    const auto weak = weak_from_this();
    try
    {
      if (actions_.post_decision(
              [weak, pending, approved, presentation_failed]
              {
                if (auto state = weak.lock())
                  state->finish(pending, approved, presentation_failed);
              }))
        return;
    }
    catch (...)
    {
      // Rejected scheduling must not strand an accepted client operation.
    }
    finish(pending, false, true);
  }

  void finish(const std::shared_ptr<Pending>& pending, bool approved, bool presentation_failed)
  {
    auto lease = enter();
    if (!lease)
      return;
    const bool expired = actions_.now() - pending->admitted_at >= maximum_client_request_age;
    {
      std::lock_guard lock(mutex_);
      if (!current(pending) || pending->executing)
        return;
      pending->executing = true;
    }
    if (expired)
      dismiss(std::vector<std::shared_ptr<Pending>>{pending});
    decide(*pending, approved && !expired, presentation_failed);
    erase(pending);
  }

  void decide(const Pending& pending, bool approved, bool failed)
  {
    const auto* role = std::get_if<RoleChangeRequest>(&pending.request);
    try
    {
      if (role)
      {
        if (actions_.decide_role)
          actions_.decide_role(*role, approved);
      }
      else if (actions_.decide_global)
        actions_.decide_global(std::get<GlobalSettingChangeRequest>(pending.request), approved);
    }
    catch (...)
    {
      failed = true;
    }
    if (failed)
      actions_.report_error(role ? "Could not complete the client role request"
                                 : "Could not complete the global setting request");
  }

  ManagementApprovalDispatchActions actions_;
  std::mutex mutex_;
  std::condition_variable idle_;
  bool accepting_ = true;
  std::size_t active_callbacks_ = 0;
  std::size_t retained_presentations_ = 0;
  std::map<Key, std::shared_ptr<Pending>> pending_;
};

ManagementApprovalDispatcher::ManagementApprovalDispatcher(
    ManagementApprovalDispatchActions actions)
    : state_(std::make_shared<State>(std::move(actions)))
{
  expiry_worker_ = std::jthread(
      [this](std::stop_token stop)
      {
        std::unique_lock lock(expiry_mutex_);
        while (!stop.stop_requested())
        {
          expiry_wake_.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
          if (stop.stop_requested())
            break;
          lock.unlock();
          state_->expire_safely();
          lock.lock();
        }
      });
}

ManagementApprovalDispatcher::~ManagementApprovalDispatcher()
{
  close();
}

ManagementApprovalRequesters ManagementApprovalDispatcher::requesters() const
{
  std::weak_ptr<State> weak = state_;
  return {[weak](RoleChangeRequest request)
          {
            auto state = weak.lock();
            return state && state->request(std::move(request));
          },
          [weak](GlobalSettingChangeRequest request)
          {
            auto state = weak.lock();
            return state && state->request(std::move(request));
          },
          [weak](std::string_view device_id)
          {
            if (auto state = weak.lock())
              state->cancel_device(device_id);
          }};
}

void ManagementApprovalDispatcher::close()
{
  expiry_worker_.request_stop();
  expiry_wake_.notify_all();
  if (expiry_worker_.joinable())
    expiry_worker_.join();
  state_->close();
}

void ManagementApprovalDispatcher::expire_requests()
{
  state_->expire();
}
} // namespace studio_remote
