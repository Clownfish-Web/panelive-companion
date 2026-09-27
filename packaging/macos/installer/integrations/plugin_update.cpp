#include "plugin_update.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

namespace studio_remote::macos::installer
{
namespace
{
namespace fs = std::filesystem;
constexpr auto transaction_prefix = ".panelive-companion-obs-plugin-update-";

class Lease final
{
public:
  explicit Lease(const fs::path& parent)
      : descriptor_(::open((parent / ".panelive-companion-obs-plugin-install.lock").c_str(),
                           O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                           0600))
  {
    if (descriptor_ < 0)
      throw std::system_error(
          errno, std::generic_category(), "Could not access shared OBS plugins");
    if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0)
    {
      const auto error = errno;
      if (::close(descriptor_) != 0)
        ::syslog(LOG_ERR, "Could not close failed OBS update lease");
      throw std::system_error(error, std::generic_category(), "Another OBS update is active");
    }
  }
  ~Lease()
  {
    // Keep the inode: unlinking a POSIX advisory lock could admit two concurrent installers.
    if (::close(descriptor_) != 0)
      ::syslog(LOG_ERR, "Could not close OBS update lease");
  }
  Lease(const Lease&) = delete;
  Lease& operator=(const Lease&) = delete;

private:
  int descriptor_;
};

std::map<fs::path, std::uintmax_t> bundle_files(const fs::path& bundle)
{
  std::map<fs::path, std::uintmax_t> files;
  for (const auto& entry : fs::recursive_directory_iterator(bundle))
  {
    const auto status = entry.symlink_status();
    if (fs::is_regular_file(status))
      files.emplace(entry.path().lexically_relative(bundle), entry.file_size());
    else if (!fs::is_directory(status))
      throw std::runtime_error(
          "OBS helper bundles must contain only directories and regular files");
  }
  return files;
}

bool same_bundle(const fs::path& source, const fs::path& destination)
{
  if (!fs::exists(destination))
    return false;
  const auto source_files = bundle_files(source);
  if (source_files != bundle_files(destination))
    return false;
  for (const auto& [relative, size] : source_files)
  {
    std::ifstream left(source / relative, std::ios::binary);
    std::ifstream right(destination / relative, std::ios::binary);
    if (!left || !right)
      throw std::runtime_error("Could not compare the installed OBS helper");
    for (std::uintmax_t offset = 0; offset < size; ++offset)
      if (left.get() != right.get())
        return false;
    if (left.bad() || right.bad() || left.fail() || right.fail())
      throw std::runtime_error("Could not read the installed OBS helper");
  }
  return true;
}

class Replacement final
{
public:
  explicit Replacement(const fs::path& destination) : destination_(destination)
  {
    auto pattern =
        (destination.parent_path() / (std::string(transaction_prefix) + "XXXXXXXX")).string();
    if (!::mkdtemp(pattern.data()))
      throw std::system_error(errno, std::generic_category(), "Could not stage the OBS helper");
    directory_ = pattern;
  }
  ~Replacement()
  {
    if (preserve_)
      return;
    std::error_code error;
    fs::remove_all(directory_, error);
    if (error)
      ::syslog(LOG_ERR, "Could not remove completed OBS update staging directory");
  }
  Replacement(const Replacement&) = delete;
  Replacement& operator=(const Replacement&) = delete;

  void apply(const fs::path& source, const BundleVerifier& verify)
  {
    const auto staged = directory_ / destination_.filename();
    fs::copy(source, staged, fs::copy_options::recursive);
    verify(staged);
    previous_ = directory_ / "previous.plugin";
    if (fs::exists(destination_))
    {
      fs::rename(destination_, previous_);
      backed_up_ = true;
    }
    fs::rename(staged, destination_);
    placed_ = true;
    verify(destination_);
    if (!same_bundle(source, destination_))
      throw std::runtime_error("Installed OBS helper differs from its verified package");
  }

  void rollback()
  {
    try
    {
      if (placed_)
        fs::remove_all(destination_);
      if (backed_up_)
        fs::rename(previous_, destination_);
    }
    catch (const fs::filesystem_error&)
    {
      preserve_ = true;
      // A rollback failure must never be treated as an access-denied retry.
      throw std::runtime_error("OBS helper rollback failed; recovery retained at " +
                               directory_.string());
    }
  }

private:
  fs::path destination_;
  fs::path directory_;
  fs::path previous_;
  bool placed_ = false;
  bool backed_up_ = false;
  bool preserve_ = false;
};
} // namespace

void update_plugin(const fs::path& source,
                   const fs::path& destination,
                   const BundleVerifier& verify,
                   const std::function<void()>& require_obs_closed,
                   const std::function<void()>& prepare_directory)
{
  if (fs::is_symlink(fs::symlink_status(source)) || fs::is_symlink(fs::symlink_status(destination)))
    throw std::runtime_error("OBS helper source and destination must not be symbolic links");
  verify(source);
  bundle_files(source);
  if (same_bundle(source, destination))
    return;
  require_obs_closed();
  prepare_directory();
  const auto parent = destination.parent_path();
  fs::create_directories(parent);
  const Lease lease(parent);
  for (const auto& entry : fs::directory_iterator(parent))
    if (entry.path().filename().string().starts_with(transaction_prefix))
      throw std::runtime_error("An interrupted OBS helper update needs recovery in " +
                               parent.string());
  Replacement replacement(destination);
  try
  {
    replacement.apply(source, verify);
  }
  catch (const std::exception&)
  {
    // Restore the transaction before propagating its original failure, including access denial.
    replacement.rollback();
    throw;
  }
}

void run_integration(bool elevated,
                     const std::function<void()>& apply,
                     const std::function<void()>& authorize)
{
  try
  {
    apply();
  }
  catch (const std::system_error& error)
  {
    if (elevated || (error.code() != std::errc::permission_denied &&
                     error.code() != std::errc::operation_not_permitted))
      throw;
    authorize();
  }
}
} // namespace studio_remote::macos::installer
