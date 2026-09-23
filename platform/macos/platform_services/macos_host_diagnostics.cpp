#include "macos_host_diagnostics.hpp"

#include "macos_network_diagnostics.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <libproc.h>
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <syslog.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace studio_remote::macos
{
namespace
{
class HostPort final
{
public:
  HostPort() : value_(mach_host_self())
  {
  }
  ~HostPort()
  {
    if (value_ != MACH_PORT_NULL && mach_port_deallocate(mach_task_self(), value_) != KERN_SUCCESS)
      syslog(LOG_ERR, "Could not release host diagnostics port");
  }
  HostPort(const HostPort&) = delete;
  HostPort& operator=(const HostPort&) = delete;
  host_t get() const noexcept
  {
    return value_;
  }

private:
  const host_t value_;
};

std::uint64_t cpu_time_100ns(const timeval& time)
{
  return static_cast<std::uint64_t>(time.tv_sec) * 10'000'000 +
         static_cast<std::uint64_t>(time.tv_usec) * 10;
}

void process_memory(json& process, json& errors)
{
  task_vm_info_data_t memory{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&memory), &count) !=
          KERN_SUCCESS ||
      count < TASK_VM_INFO_REV1_COUNT)
  {
    errors.push_back("processMemoryUnavailable");
    return;
  }
  process["workingSetBytes"] = memory.resident_size;
  process["peakWorkingSetBytes"] = memory.resident_size_peak;
  // Footprint is native charged physical memory, not Windows committed PrivateUsage.
  process["physicalFootprintBytes"] = memory.phys_footprint;
}

void process_identity(json& process, json& errors)
{
  proc_taskallinfo info{};
  if (proc_pidinfo(getpid(), PROC_PIDTASKALLINFO, 0, &info, sizeof(info)) != sizeof(info))
  {
    errors.push_back("processIdentityUnavailable");
    return;
  }
  process["threadCount"] = info.ptinfo.pti_threadnum;
  constexpr std::uint64_t filetime_epoch_seconds = 11'644'473'600;
  process["createdFileTime"] = (info.pbsd.pbi_start_tvsec + filetime_epoch_seconds) * 10'000'000 +
                               info.pbsd.pbi_start_tvusec * 10;
  const auto created_ms = info.pbsd.pbi_start_tvsec * 1000 + info.pbsd.pbi_start_tvusec / 1000;
  const auto now_ms =
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
  process["uptimeMs"] = now_ms >= created_ms ? now_ms - created_ms : 0;
}

void process_descriptors(json& process, json& errors)
{
  const auto bytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
  constexpr int maximum_bytes = 65'536 * sizeof(proc_fdinfo);
  if (bytes <= 0 || bytes > maximum_bytes)
  {
    errors.push_back("processDescriptorsUnavailable");
    return;
  }
  std::vector<proc_fdinfo> descriptors(static_cast<std::size_t>(bytes) / sizeof(proc_fdinfo) + 1);
  const auto actual = proc_pidinfo(getpid(),
                                   PROC_PIDLISTFDS,
                                   0,
                                   descriptors.data(),
                                   static_cast<int>(descriptors.size() * sizeof(proc_fdinfo)));
  if (actual <= 0 || static_cast<std::size_t>(actual) > descriptors.size() * sizeof(proc_fdinfo) ||
      actual % sizeof(proc_fdinfo) != 0)
    errors.push_back("processDescriptorsUnavailable");
  else
    process["fileDescriptorCount"] = static_cast<std::size_t>(actual) / sizeof(proc_fdinfo);
}

json process_snapshot(json& errors)
{
  json process = {{"pid", getpid()}};
  process_memory(process, errors);
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0)
  {
    process["kernelTime100ns"] = cpu_time_100ns(usage.ru_stime);
    process["userTime100ns"] = cpu_time_100ns(usage.ru_utime);
    process["pageFaults"] = static_cast<std::uint64_t>(usage.ru_minflt) + usage.ru_majflt;
  }
  else
    errors.push_back("processCpuUnavailable");
  process_identity(process, errors);
  rusage_info_v2 io{};
  if (proc_pid_rusage(getpid(), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&io)) == 0)
  {
    process["diskReadBytes"] = io.ri_diskio_bytesread;
    process["diskWriteBytes"] = io.ri_diskio_byteswritten;
  }
  else
    errors.push_back("processIoUnavailable");
  process_descriptors(process, errors);
  return process;
}

void host_memory(json& host, json& errors)
{
  std::uint64_t total = 0;
  auto size = sizeof(total);
  vm_statistics64_data_t memory{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_size_t page_size = 0;
  HostPort port;
  if (sysctlbyname("hw.memsize", &total, &size, nullptr, 0) != 0 || size != sizeof(total) ||
      total == 0 || host_page_size(port.get(), &page_size) != KERN_SUCCESS ||
      host_statistics64(
          port.get(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&memory), &count) !=
          KERN_SUCCESS ||
      count < HOST_VM_INFO64_REV0_COUNT || page_size == 0)
    errors.push_back("hostMemoryUnavailable");
  else
  {
    const auto available = std::min(
        total, (static_cast<std::uint64_t>(memory.free_count) + memory.inactive_count) * page_size);
    host["availablePhysicalBytes"] = available;
    host["memoryLoadPercent"] = (total - available) * 100 / total;
  }
  xsw_usage swap{};
  size = sizeof(swap);
  if (sysctlbyname("vm.swapusage", &swap, &size, nullptr, 0) == 0 && size == sizeof(swap))
    host["availableSwapBytes"] = swap.xsu_avail;
  else
    errors.push_back("hostSwapUnavailable");
}

std::optional<int> capacity(CFDictionaryRef source, CFStringRef key)
{
  const auto value = CFDictionaryGetValue(source, key);
  int result = 0;
  if (!value || CFGetTypeID(value) != CFNumberGetTypeID() ||
      !CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &result))
    return std::nullopt;
  return result;
}

void host_power(json& host, json& errors)
{
  std::unique_ptr<const void, decltype(&CFRelease)> power(IOPSCopyPowerSourcesInfo(), CFRelease);
  if (!power)
  {
    errors.push_back("hostPowerUnavailable");
    return;
  }
  const auto source_type = IOPSGetProvidingPowerSourceType(power.get());
  host["acLineOnline"] = source_type && CFEqual(source_type, CFSTR(kIOPSACPowerValue));
  host["batteryPercent"] = nullptr;
  std::unique_ptr<const __CFArray, decltype(&CFRelease)> sources(
      IOPSCopyPowerSourcesList(power.get()), CFRelease);
  if (!sources)
  {
    errors.push_back("hostBatteryUnavailable");
    return;
  }
  for (CFIndex index = 0; index < CFArrayGetCount(sources.get()); ++index)
  {
    const auto description =
        IOPSGetPowerSourceDescription(power.get(), CFArrayGetValueAtIndex(sources.get(), index));
    if (!description)
      continue;
    const auto type = CFDictionaryGetValue(description, CFSTR(kIOPSTypeKey));
    if (!type || !CFEqual(type, CFSTR(kIOPSInternalBatteryType)))
      continue;
    const auto current = capacity(description, CFSTR(kIOPSCurrentCapacityKey));
    const auto maximum = capacity(description, CFSTR(kIOPSMaxCapacityKey));
    if (current && maximum && *current >= 0 && *maximum > 0)
      host["batteryPercent"] = std::min(std::int64_t{100}, std::int64_t{*current} * 100 / *maximum);
    else
      errors.push_back("hostBatteryCapacityUnavailable");
    break;
  }
}
} // namespace

json macos_host_diagnostics()
{
  json errors = json::array();
  auto process = process_snapshot(errors);
  json host = json::object();
  host_memory(host, errors);
  host_power(host, errors);
  auto interfaces = macos_network_diagnostics(errors);
  return {{"process", std::move(process)},
          {"host", std::move(host)},
          {"networkInterfaces", std::move(interfaces)},
          {"observationErrors", std::move(errors)}};
}
} // namespace studio_remote::macos
