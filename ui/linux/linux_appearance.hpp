#pragma once

#include <gtk/gtk.h>

#include <memory>
#include <string>

namespace studio_remote::linux
{
// GTK-thread owner of palette observation. It borrows the application display and never owns
// management state. Destroy before releasing the display or application.
class LinuxAppearance
{
public:
  LinuxAppearance();
  ~LinuxAppearance();
  LinuxAppearance(const LinuxAppearance&) = delete;
  LinuxAppearance& operator=(const LinuxAppearance&) = delete;

private:
  struct MonitorDeleter
  {
    void operator()(GFileMonitor* monitor) const noexcept;
  };
  struct ProviderDeleter
  {
    void operator()(GtkCssProvider* provider) const noexcept;
  };
  using Monitor = std::unique_ptr<GFileMonitor, MonitorDeleter>;
  void watch(Monitor& monitor, const std::string& path);
  void clear_watch(Monitor& monitor) noexcept;
  void reload() noexcept;
  void schedule_reload() noexcept;
  void report_failure(const char* message) noexcept;

  GdkDisplay* display_;
  std::string current_directory_;
  std::string applied_stylesheet_;
  std::unique_ptr<GtkCssProvider, ProviderDeleter> provider_;
  Monitor current_monitor_;
  Monitor theme_monitor_;
  guint reload_source_ = 0;
  bool failure_reported_ = false;
};
} // namespace studio_remote::linux
