#include "windows_host_diagnostics.hpp"

// Windows networking headers must precede Windows.h.
// clang-format off
#include <winsock2.h>
#include <ws2ipdef.h>
#include <Windows.h>
#include <iphlpapi.h>
// clang-format on
#include <TlHelp32.h>
#include <psapi.h>

#include <array>
#include <vector>

namespace studio_remote::windows
{
namespace
{
std::uint64_t file_time_value(const FILETIME& value)
{
  ULARGE_INTEGER result{};
  result.LowPart = value.dwLowDateTime;
  result.HighPart = value.dwHighDateTime;
  return result.QuadPart;
}

std::string interface_type(ULONG type)
{
  switch (type)
  {
  case IF_TYPE_ETHERNET_CSMACD:
    return "ethernet";
  case IF_TYPE_IEEE80211:
    return "wifi";
  case IF_TYPE_SOFTWARE_LOOPBACK:
    return "loopback";
  case IF_TYPE_PPP:
    return "ppp";
  case IF_TYPE_TUNNEL:
    return "tunnel";
  default:
    return "other";
  }
}
} // namespace

json windows_host_diagnostics()
{
  json process = {{"pid", GetCurrentProcessId()}};
  PROCESS_MEMORY_COUNTERS_EX memory{};
  memory.cb = sizeof(memory);
  if (GetProcessMemoryInfo(
          GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
  {
    process["workingSetBytes"] = memory.WorkingSetSize;
    process["peakWorkingSetBytes"] = memory.PeakWorkingSetSize;
    process["privateBytes"] = memory.PrivateUsage;
    process["pageFaults"] = memory.PageFaultCount;
  }

  FILETIME created{}, exited{}, kernel{}, user{};
  if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
  {
    process["createdFileTime"] = file_time_value(created);
    process["kernelTime100ns"] = file_time_value(kernel);
    process["userTime100ns"] = file_time_value(user);
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const auto created_value = file_time_value(created);
    const auto now_value = file_time_value(now);
    process["uptimeMs"] = now_value >= created_value ? (now_value - created_value) / 10'000 : 0;
  }

  IO_COUNTERS io{};
  if (GetProcessIoCounters(GetCurrentProcess(), &io))
  {
    process["readOperations"] = io.ReadOperationCount;
    process["writeOperations"] = io.WriteOperationCount;
    process["readBytes"] = io.ReadTransferCount;
    process["writeBytes"] = io.WriteTransferCount;
  }
  process["handleCount"] = 0;
  DWORD handles = 0;
  if (GetProcessHandleCount(GetCurrentProcess(), &handles))
    process["handleCount"] = handles;
  std::uint32_t threads = 0;
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot != INVALID_HANDLE_VALUE)
  {
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry))
      do
      {
        if (entry.th32OwnerProcessID == GetCurrentProcessId())
          ++threads;
      } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
  }
  process["threadCount"] = threads;

  json host;
  MEMORYSTATUSEX host_memory{};
  host_memory.dwLength = sizeof(host_memory);
  if (GlobalMemoryStatusEx(&host_memory))
  {
    host["memoryLoadPercent"] = host_memory.dwMemoryLoad;
    host["availablePhysicalBytes"] = host_memory.ullAvailPhys;
    host["availablePageFileBytes"] = host_memory.ullAvailPageFile;
  }
  SYSTEM_POWER_STATUS power{};
  if (GetSystemPowerStatus(&power))
  {
    host["acLineOnline"] = power.ACLineStatus == 1;
    host["batteryPercent"] =
        power.BatteryLifePercent == 255 ? json(nullptr) : json(power.BatteryLifePercent);
  }

  json interfaces = json::array();
  MIB_IF_TABLE2* table = nullptr;
  if (GetIfTable2(&table) == NO_ERROR && table)
  {
    for (ULONG index = 0; index < table->NumEntries; ++index)
    {
      const auto& row = table->Table[index];
      if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp)
        continue;
      interfaces.push_back({{"interfaceIndex", row.InterfaceIndex},
                            {"type", interface_type(row.Type)},
                            {"connected", row.MediaConnectState == MediaConnectStateConnected},
                            {"transmitLinkBitsPerSecond", row.TransmitLinkSpeed},
                            {"receiveLinkBitsPerSecond", row.ReceiveLinkSpeed},
                            {"outOctets", row.OutOctets},
                            {"inOctets", row.InOctets},
                            {"outErrors", row.OutErrors},
                            {"inErrors", row.InErrors},
                            {"outDiscards", row.OutDiscards},
                            {"inDiscards", row.InDiscards}});
    }
    FreeMibTable(table);
  }

  return {{"process", std::move(process)},
          {"host", std::move(host)},
          {"networkInterfaces", std::move(interfaces)}};
}

} // namespace studio_remote::windows
