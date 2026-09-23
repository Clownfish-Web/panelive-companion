#pragma once
#include <Windows.h>
#include <atomic>
#include <studio_remote/discovery/service_discovery.hpp>
#include <vector>
#include <windns.h>
namespace studio_remote::windows
{
class WindowsDnsSd final : public IServiceDiscovery
{
public:
  WindowsDnsSd() = default;
  ~WindowsDnsSd() override;
  void advertise(const ServiceRecord& record) override;
  void stop() override;

private:
  static void completed(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance);
  DNS_SERVICE_REGISTER_REQUEST request_{};
  PDNS_SERVICE_INSTANCE instance_ = nullptr;
  std::vector<std::wstring> key_storage_, value_storage_;
  std::vector<PCWSTR> keys_, values_;
  std::atomic<DWORD> status_{ERROR_IO_PENDING};
};
} // namespace studio_remote::windows
