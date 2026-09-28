#include "linux_autostart.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace studio_remote::linux
{
namespace
{
std::filesystem::path config_home()
{
  if (const auto* configured = std::getenv("XDG_CONFIG_HOME"); configured && *configured)
    return configured;
  if (const auto* home = std::getenv("HOME"); home && *home)
    return std::filesystem::path(home) / ".config";
  throw std::runtime_error("Neither XDG_CONFIG_HOME nor HOME is available");
}

std::string startup_command()
{
  const auto* launcher = std::getenv("PANELIVE_LINUX_LAUNCHER");
  if (!launcher || !*launcher)
    return "panelive-companion";
  // Desktop Entry Exec quoting differs from shell quoting. A literal percent
  // must also be doubled so it cannot become a field code.
  std::string command = "\"";
  for (const char c : std::string(launcher))
  {
    if (c == '\n' || c == '\r')
      throw std::runtime_error("The application path cannot contain a newline");
    if (c == '\\')
      command += "\\\\\\\\";
    else if (c == '"' || c == '`' || c == '$')
    {
      command += "\\\\";
      command += c;
    }
    else if (c == '%')
      command += "%%";
    else
      command += c;
  }
  return command + "\"";
}
} // namespace

SignInStartup::SignInStartup()
    : path_(config_home() / "autostart" / "panelive-companion-autostart.desktop")
{
}

bool SignInStartup::enabled() const
{
  return std::filesystem::is_regular_file(path_);
}

void SignInStartup::set_enabled(bool enabled) const
{
  if (!enabled)
  {
    std::error_code error;
    std::filesystem::remove(path_, error);
    if (error)
      throw std::runtime_error("Could not disable desktop-session startup: " + error.message());
    return;
  }
  const auto command = startup_command();
  std::filesystem::create_directories(path_.parent_path());
  const auto temporary = path_.string() + ".tmp";
  {
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream)
      throw std::runtime_error("Could not create desktop-session startup entry");
    stream << "[Desktop Entry]\n"
              "Type=Application\n"
              "Name=PaneLive Companion\n"
           << "Exec=" << command << '\n'
           << "Icon=com.clownfishweb.PaneLiveCompanion\n"
              "Terminal=false\n"
              "X-GNOME-Autostart-enabled=true\n"
              "NoDisplay=true\n";
  }
  std::filesystem::rename(temporary, path_);
}

const std::filesystem::path& SignInStartup::path() const noexcept
{
  return path_;
}
} // namespace studio_remote::linux
