#include "linux_appearance.hpp"

#include "omarchy_palette.hpp"

#include <adwaita.h>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace studio_remote::linux
{
namespace
{
class PaletteDescriptor
{
public:
  explicit PaletteDescriptor(const std::string& path)
      : descriptor_(open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW))
  {
    if (descriptor_ < 0)
      throw std::runtime_error("Omarchy palette is temporarily unavailable");
  }
  ~PaletteDescriptor()
  {
    close(descriptor_);
  }
  PaletteDescriptor(const PaletteDescriptor&) = delete;
  PaletteDescriptor& operator=(const PaletteDescriptor&) = delete;
  int get() const noexcept
  {
    return descriptor_;
  }

private:
  int descriptor_;
};

std::string read_palette(const std::string& path)
{
  PaletteDescriptor descriptor(path);
  struct stat metadata{};
  if (fstat(descriptor.get(), &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
      metadata.st_size < 1 || metadata.st_size > 16'384)
    throw std::runtime_error("Omarchy palette must be a regular file of at most 16 KiB");
  std::array<char, 16'385> contents{};
  std::size_t size = 0;
  while (size < contents.size())
  {
    const auto count = read(descriptor.get(), contents.data() + size, contents.size() - size);
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0)
      throw std::runtime_error("Could not read Omarchy palette");
    if (count == 0)
      break;
    size += static_cast<std::size_t>(count);
  }
  return {contents.data(), size};
}
} // namespace

void LinuxAppearance::MonitorDeleter::operator()(GFileMonitor* monitor) const noexcept
{
  g_file_monitor_cancel(monitor);
  g_object_unref(monitor);
}

void LinuxAppearance::ProviderDeleter::operator()(GtkCssProvider* provider) const noexcept
{
  g_object_unref(provider);
}

LinuxAppearance::LinuxAppearance()
    : display_(gdk_display_get_default()),
      current_directory_(std::string(g_get_home_dir()) + "/.local/state/omarchy/current"),
      provider_(gtk_css_provider_new())
{
  adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_DEFAULT);
  gtk_style_context_add_provider_for_display(
      display_, GTK_STYLE_PROVIDER(provider_.get()), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
  if (g_file_test(current_directory_.c_str(), G_FILE_TEST_IS_DIR))
  {
    watch(current_monitor_, current_directory_);
    reload();
  }
}

LinuxAppearance::~LinuxAppearance()
{
  if (reload_source_)
    g_source_remove(reload_source_);
  clear_watch(theme_monitor_);
  clear_watch(current_monitor_);
  gtk_style_context_remove_provider_for_display(display_, GTK_STYLE_PROVIDER(provider_.get()));
}

void LinuxAppearance::clear_watch(Monitor& monitor) noexcept
{
  if (monitor)
    g_signal_handlers_disconnect_by_data(monitor.get(), this);
  monitor.reset();
}

void LinuxAppearance::watch(Monitor& monitor, const std::string& path)
{
  clear_watch(monitor);
  const std::unique_ptr<GFile, decltype(&g_object_unref)> directory(
      g_file_new_for_path(path.c_str()), g_object_unref);
  GError* error = nullptr;
  monitor.reset(
      g_file_monitor_directory(directory.get(), G_FILE_MONITOR_WATCH_MOVES, nullptr, &error));
  if (error)
  {
    report_failure("Could not observe Omarchy palette changes");
    g_clear_error(&error);
  }
  if (monitor)
    g_signal_connect(
        monitor.get(),
        "changed",
        G_CALLBACK(+[](GFileMonitor*, GFile*, GFile*, GFileMonitorEvent, gpointer data) noexcept
                   { static_cast<LinuxAppearance*>(data)->schedule_reload(); }),
        this);
}

void LinuxAppearance::schedule_reload() noexcept
{
  if (reload_source_)
    return;
  reload_source_ = g_timeout_add(
      120,
      +[](gpointer data) noexcept -> gboolean
      {
        auto& owner = *static_cast<LinuxAppearance*>(data);
        owner.reload_source_ = 0;
        owner.reload();
        return G_SOURCE_REMOVE;
      },
      this);
}

void LinuxAppearance::report_failure(const char* message) noexcept
{
  if (!failure_reported_)
    g_warning("%s; retaining the last valid appearance", message);
  failure_reported_ = true;
}

void LinuxAppearance::reload() noexcept
{
  try
  {
    watch(theme_monitor_, current_directory_ + "/theme");
    const auto palette =
        parse_omarchy_palette(read_palette(current_directory_ + "/theme/colors.toml"));
    const auto stylesheet = omarchy_palette_stylesheet(palette);
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(),
                                       palette.light ? ADW_COLOR_SCHEME_FORCE_LIGHT
                                                     : ADW_COLOR_SCHEME_FORCE_DARK);
    if (stylesheet != applied_stylesheet_)
    {
      G_GNUC_BEGIN_IGNORE_DEPRECATIONS
      gtk_css_provider_load_from_data(
          provider_.get(), stylesheet.c_str(), static_cast<gssize>(stylesheet.size()));
      G_GNUC_END_IGNORE_DEPRECATIONS
      applied_stylesheet_ = stylesheet;
    }
    failure_reported_ = false;
  }
  catch (const std::exception& error)
  {
    report_failure(error.what());
  }
  catch (...)
  {
    report_failure("Omarchy appearance update failed");
  }
}
} // namespace studio_remote::linux
