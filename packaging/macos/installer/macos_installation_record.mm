#include "macos_installation_record.hpp"

#include "../../../platform/macos/platform_services/macos_production_installations.hpp"

#import <Foundation/Foundation.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

namespace studio_remote::macos::installer
{
namespace
{
void fail(const char* message)
{
  throw std::system_error(errno, std::generic_category(), message);
}

class Descriptor final
{
public:
  explicit Descriptor(int value) : value_(value)
  {
    if (value_ < 0)
      fail("Could not open installation record storage");
  }
  ~Descriptor()
  {
    if (::close(value_) != 0)
      ::syslog(LOG_ERR, "Could not close installation record descriptor");
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int get() const noexcept
  {
    return value_;
  }

private:
  int value_;
};

std::string temporary_name()
{
  std::array<unsigned char, 16> bytes{};
  ::arc4random_buf(bytes.data(), bytes.size());
  std::string result = ".panelive-companion-install-";
  constexpr char hex[] = "0123456789abcdef";
  for (const auto value : bytes)
  {
    result += hex[value >> 4];
    result += hex[value & 15];
  }
  return result;
}

class StagedRecord final
{
public:
  explicit StagedRecord(int directory)
      : directory_(directory), name_(temporary_name()),
        descriptor_(::openat(
            directory, name_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600))
  {
  }
  ~StagedRecord()
  {
    if (!committed_ && ::unlinkat(directory_, name_.c_str(), 0) != 0)
      ::syslog(LOG_ERR, "Could not remove temporary installation record");
  }
  StagedRecord(const StagedRecord&) = delete;
  StagedRecord& operator=(const StagedRecord&) = delete;

  void write(std::string_view contents)
  {
    while (!contents.empty())
    {
      const auto count = ::write(descriptor_.get(), contents.data(), contents.size());
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0)
        fail("Could not write installation record");
      contents.remove_prefix(static_cast<std::size_t>(count));
    }
    // Machine installation metadata is readable by normal Companion users, never writable by them.
    if (::fchmod(descriptor_.get(), 0644) != 0)
      fail("Could not set installation record permissions");
  }

  void commit(const std::string& filename)
  {
    if (::fsync(descriptor_.get()) != 0)
      fail("Could not synchronize installation record");
    if (::renameat(directory_, name_.c_str(), directory_, filename.c_str()) != 0)
      fail("Could not replace installation record");
    committed_ = true;
    if (::fsync(directory_) != 0)
      throw InstallationRecordCommitError(errno);
  }

private:
  const int directory_;
  const std::string name_;
  Descriptor descriptor_;
  bool committed_ = false;
};

NSString* native_path(const std::filesystem::path& path)
{
  const auto text = path.string();
  if (text.empty() || text.find('\0') != std::string::npos)
    throw std::invalid_argument("Installation record paths must be nonempty and contain no NUL");
  NSString* value = [[NSString alloc] initWithBytes:text.data()
                                             length:text.size()
                                           encoding:NSUTF8StringEncoding];
  if (!value)
    throw std::invalid_argument("Installation record paths must contain valid UTF-8");
  return value;
}

std::string encode_record(const InstallationRecord& record)
{
  if (record.installed_providers != "obs,streamlabs,companion-teleprompter")
    throw std::invalid_argument("Unsupported installed providers");
  NSDictionary* values = @{
    @"InstallDirectory" : native_path(record.install_directory),
    @"InstalledProviders" : [NSString stringWithUTF8String:record.installed_providers.c_str()],
    @"ObsDirectory" : native_path(record.obs_directory)
  };
  NSData* data = [NSPropertyListSerialization dataWithPropertyList:values
                                                            format:NSPropertyListXMLFormat_v1_0
                                                           options:0
                                                             error:nullptr];
  if (!data)
    throw std::runtime_error("Could not encode installation record");
  if (data.length > 64 * 1024)
    throw std::invalid_argument("Installation record exceeds the maximum size");
  return {static_cast<const char*>(data.bytes), data.length};
}
} // namespace

void write_installation_record(const InstallationRecord& record)
{
  write_installation_record(record, installation_record_file());
}

void write_installation_record(const InstallationRecord& record,
                               const std::filesystem::path& record_file)
{
  @autoreleasepool
  {
    const auto contents = encode_record(record);
    const auto filename = record_file.filename().string();
    if (filename.empty() || filename == "." || filename == ".." ||
        filename.find('\0') != std::string::npos)
      throw std::invalid_argument("Installation record filename is invalid");
    // The shared Preferences directory is installer-owned externally, not a private config folder.
    const Descriptor directory(
        ::open(record_file.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    StagedRecord staged(directory.get());
    staged.write(contents);
    staged.commit(filename);
  }
}
} // namespace studio_remote::macos::installer
