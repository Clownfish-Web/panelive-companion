#pragma once

#include <gtk/gtk.h>
#include <studio_remote/ui/management_view.hpp>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace studio_remote::linux
{
/// GTK-thread-owned equivalent of the Windows approval dialog registry.
class LinuxApprovalDialogs final
{
public:
  ~LinuxApprovalDialogs();
  void show(GtkWindow* parent,
            std::string device_id,
            std::string request_id,
            std::string_view title,
            std::string_view message,
            ManagementApprovalDecision decision);
  void cancel(std::string_view device_id, std::string_view request_id);
  void close();

private:
  using Key = std::pair<std::string, std::string>;
  struct WindowDeleter
  {
    void operator()(GtkWindow* window) const noexcept;
  };
  struct Pending
  {
    std::unique_ptr<GtkWindow, WindowDeleter> window;
    ManagementApprovalDecision decision;
  };
  void finish(const Key& key, bool approved) noexcept;
  std::map<Key, Pending> pending_;
};
} // namespace studio_remote::linux
