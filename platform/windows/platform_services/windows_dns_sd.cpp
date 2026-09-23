#include "windows_dns_sd.hpp"
#include <stdexcept>
#include <studio_remote/discovery/companion_service_record.hpp>
namespace studio_remote::windows
{
namespace
{
std::wstring wide(std::string_view text)
{
  if (text.empty())
    return {};
  int count =
      MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(count, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
  return result;
}
} // namespace
WindowsDnsSd::~WindowsDnsSd()
{
  stop();
}
void WindowsDnsSd::advertise(const ServiceRecord& record)
{
  const auto type = validate_dns_sd_record(record);
  stop();
  key_storage_.clear();
  value_storage_.clear();
  keys_.clear();
  values_.clear();
  for (const auto& [key, value] : record.txt)
  {
    key_storage_.push_back(wide(key));
    value_storage_.push_back(wide(value));
  }
  for (auto& value : key_storage_)
    keys_.push_back(value.c_str());
  for (auto& value : value_storage_)
    values_.push_back(value.c_str());
  wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
  DWORD length = MAX_COMPUTERNAME_LENGTH + 1;
  GetComputerNameW(computer, &length);
  std::wstring host(computer, length);
  host += L".local";
  auto service = wide(record.instance_name + "." + type + ".local");
  instance_ = DnsServiceConstructInstance(service.c_str(),
                                          host.c_str(),
                                          nullptr,
                                          nullptr,
                                          record.port,
                                          0,
                                          0,
                                          static_cast<DWORD>(keys_.size()),
                                          keys_.data(),
                                          values_.data());
  if (!instance_)
    throw std::runtime_error("Could not construct DNS-SD instance");
  request_ = {};
  request_.Version = DNS_QUERY_REQUEST_VERSION1;
  request_.InterfaceIndex = 0;
  request_.pServiceInstance = instance_;
  request_.pRegisterCompletionCallback = &WindowsDnsSd::completed;
  request_.pQueryContext = this;
  status_ = DnsServiceRegister(&request_, nullptr);
  if (status_ != DNS_REQUEST_PENDING && status_ != ERROR_SUCCESS)
    throw std::runtime_error("DNS-SD registration failed");
}
void WindowsDnsSd::stop()
{
  if (instance_)
  {
    DnsServiceDeRegister(&request_, nullptr);
    DnsServiceFreeInstance(instance_);
    instance_ = nullptr;
  }
  keys_.clear();
  values_.clear();
}
void WindowsDnsSd::completed(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
  if (context)
  {
    auto* self = static_cast<WindowsDnsSd*>(context);
    self->status_ = status;
    if (instance && instance != self->instance_)
      DnsServiceFreeInstance(instance);
  }
  else if (instance)
    DnsServiceFreeInstance(instance);
}
} // namespace studio_remote::windows
