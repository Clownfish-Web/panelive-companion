#include "native_operations.hpp"

#import <Foundation/Foundation.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace studio_remote::macos::installer
{
namespace
{
class Child final
{
public:
  explicit Child(pid_t process) : process_(process)
  {
  }
  ~Child()
  {
    if (process_ <= 0)
      return;
    if (::kill(process_, SIGKILL) != 0 && errno != ESRCH)
      NSLog(@"Could not terminate installer child");
    while (::waitpid(process_, nullptr, 0) < 0)
      if (errno != EINTR)
        break;
  }
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;
  int wait()
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline)
    {
      int status = 0;
      const auto result = ::waitpid(process_, &status, WNOHANG);
      if (result == process_)
      {
        process_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
      }
      if (result < 0 && errno != EINTR)
        throw std::system_error(
            errno, std::generic_category(), "Could not wait for installer tool");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Installer tool timed out");
  }

private:
  pid_t process_;
};

class FileActions final
{
public:
  FileActions()
  {
    const auto error = ::posix_spawn_file_actions_init(&actions_);
    if (error)
      throw std::system_error(
          error, std::generic_category(), "Could not initialize installer tool");
  }
  ~FileActions()
  {
    if (::posix_spawn_file_actions_destroy(&actions_) != 0)
      NSLog(@"Could not release installer tool file actions");
  }
  FileActions(const FileActions&) = delete;
  FileActions& operator=(const FileActions&) = delete;
  posix_spawn_file_actions_t* get()
  {
    return &actions_;
  }

private:
  posix_spawn_file_actions_t actions_;
};
} // namespace

int run_tool(const std::string& executable, const std::vector<std::string>& arguments)
{
  std::vector<char*> argv{const_cast<char*>(executable.c_str())};
  for (const auto& argument : arguments)
    argv.push_back(const_cast<char*>(argument.c_str()));
  argv.push_back(nullptr);
  char path[] = "PATH=/usr/bin:/bin:/usr/sbin:/sbin";
  char locale[] = "LC_ALL=C";
  char* environment[]{path, locale, nullptr};
  FileActions actions;
  for (const int descriptor : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO})
  {
    const auto error =
        ::posix_spawn_file_actions_addopen(actions.get(),
                                           descriptor,
                                           "/dev/null",
                                           descriptor == STDIN_FILENO ? O_RDONLY : O_WRONLY,
                                           0);
    if (error)
      throw std::system_error(error, std::generic_category(), "Could not redirect installer tool");
  }
  pid_t process = -1;
  const auto error =
      ::posix_spawn(&process, executable.c_str(), actions.get(), nullptr, argv.data(), environment);
  if (error)
    throw std::system_error(error, std::generic_category(), "Could not launch installer tool");
  Child child(process);
  return child.wait();
}

void verify_bundle(const std::filesystem::path& bundle)
{
  if (run_tool("/usr/bin/codesign", {"--verify", "--strict", bundle.string()}) != 0)
    throw std::runtime_error("OBS integration bundle signature is invalid: " + bundle.string());
}

void require_obs_closed()
{
  const auto status = run_tool("/usr/bin/pgrep", {"-x", "OBS"});
  if (status == 0)
    throw std::runtime_error("Close OBS Studio before changing its plugin");
  if (status != 1)
    throw std::runtime_error("Could not inspect the OBS process");
}

namespace
{
enum class DirectoryOperation
{
  inspect,
  create
};

void shared_plugin_directory(DirectoryOperation operation)
{
  // Each validated ancestor is root-owned and not writable by another user. Once validated,
  // an unelevated account cannot substitute a symlink between this check and the next step.
  std::filesystem::path path;
  for (const auto* component : {"/Library", "Application Support", "obs-studio", "plugins"})
  {
    path /= component;
    if (operation == DirectoryOperation::create && ::mkdir(path.c_str(), 0755) != 0 &&
        errno != EEXIST)
      throw std::system_error(
          errno, std::generic_category(), "Could not create shared OBS storage");
    struct stat metadata{};
    if (::lstat(path.c_str(), &metadata) != 0)
    {
      if (errno == ENOENT && operation == DirectoryOperation::inspect)
        return;
      throw std::system_error(
          errno, std::generic_category(), "Could not inspect shared OBS storage");
    }
    if (!S_ISDIR(metadata.st_mode) || metadata.st_uid != 0 || (metadata.st_mode & 0022) != 0)
      throw std::runtime_error(
          "Shared OBS storage must be a root-owned directory without user write access");
  }
}

} // namespace

void validate_shared_plugin_directory()
{
  shared_plugin_directory(DirectoryOperation::inspect);
}

void ensure_shared_plugin_directory()
{
  shared_plugin_directory(DirectoryOperation::create);
}

void authorize_integration(const std::filesystem::path& executable)
{
  @autoreleasepool
  {
    NSString* path = [NSString stringWithUTF8String:executable.c_str()];
    if (!path)
      throw std::runtime_error("Invalid integration executable path");
    // Escape an AppleScript string, then let its quoted-form operation quote the shell argument.
    path = [path stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"];
    path = [path stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""];
    path = [path stringByReplacingOccurrencesOfString:@"\n" withString:@"\\n"];
    path = [path stringByReplacingOccurrencesOfString:@"\r" withString:@"\\r"];
    NSString* source = [NSString stringWithFormat:@"with timeout of 600 seconds\n"
                                                   "do shell script (quoted form of \"%@\" & \" "
                                                   "--elevated\") with administrator privileges\n"
                                                   "end timeout",
                                                  path];
    NSDictionary* error = nil;
    NSAppleScript* script = [[NSAppleScript alloc] initWithSource:source];
    if (![script executeAndReturnError:&error])
    {
      if ([error[NSAppleScriptErrorNumber] integerValue] == -128)
        throw std::runtime_error(
            "OBS helper administrator approval was cancelled; run setup again to finish");
      NSString* message = error[NSAppleScriptErrorMessage];
      throw std::runtime_error(
          "Shared OBS helper installation did not complete: " +
          std::string(message.UTF8String ? message.UTF8String : "native authorization failed"));
    }
  }
}
} // namespace studio_remote::macos::installer
