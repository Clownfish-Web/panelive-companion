#include "macos_dns_sd.hpp"

#include <studio_remote/discovery/companion_service_record.hpp>

#include <arpa/inet.h>
#include <atomic>
#include <dispatch/dispatch.h>
#include <dns_sd.h>
#include <limits>
#include <stdexcept>

namespace studio_remote::macos
{
namespace
{
constexpr std::int32_t stopped = std::numeric_limits<std::int32_t>::min();
constexpr std::int32_t registering = stopped + 1;

class TxtRecord
{
public:
  TxtRecord()
  {
    TXTRecordCreate(&record_, 0, nullptr);
  }
  void encode(const ServiceRecord& service)
  {
    for (const auto& [key, value] : service.txt)
    {
      if (TXTRecordSetValue(
              &record_, key.c_str(), static_cast<std::uint8_t>(value.size()), value.data()) !=
          kDNSServiceErr_NoError)
        throw std::runtime_error("Could not encode DNS-SD TXT record");
    }
  }
  ~TxtRecord()
  {
    TXTRecordDeallocate(&record_);
  }
  TxtRecord(const TxtRecord&) = delete;
  TxtRecord& operator=(const TxtRecord&) = delete;
  const void* bytes() const
  {
    return TXTRecordGetBytesPtr(&record_);
  }
  std::uint16_t size() const
  {
    return TXTRecordGetLength(&record_);
  }

private:
  TXTRecordRef record_{};
};

} // namespace

class MacosDnsSd::Impl
{
public:
  explicit Impl(std::uint32_t interface_index)
      : interface_index_(interface_index),
        queue_(
            dispatch_queue_create("panelive-companion.bonjour", DISPATCH_QUEUE_SERIAL))
  {
    if (!queue_)
      throw std::runtime_error("Could not create Bonjour delivery queue");
  }
  ~Impl()
  {
    stop();
  }

  void advertise(const ServiceRecord& record)
  {
    @autoreleasepool
    {
      const auto type = validate_dns_sd_record(record);
      TxtRecord text;
      text.encode(record);
      const auto* encoded = &text;
      __block DNSServiceErrorType error = kDNSServiceErr_NoError;
      dispatch_sync(queue_, ^{
        registration_.reset();
        state_.store(registering);
        DNSServiceRef raw = nullptr;
        error = DNSServiceRegister(&raw,
                                   kDNSServiceFlagsNoAutoRename,
                                   interface_index_,
                                   record.instance_name.c_str(),
                                   type.c_str(),
                                   "local.",
                                   nullptr,
                                   htons(record.port),
                                   encoded->size(),
                                   encoded->bytes(),
                                   &Impl::registered,
                                   this);
        if (error == kDNSServiceErr_NoError)
        {
          registration_.reset(raw);
          error = DNSServiceSetDispatchQueue(raw, queue_);
        }
        if (error != kDNSServiceErr_NoError)
        {
          registration_.reset();
          state_.store(error);
        }
      });
      if (error != kDNSServiceErr_NoError)
        throw std::runtime_error("Bonjour registration failed (" + std::to_string(error) + ")");
    }
  }

  void stop()
  {
    dispatch_sync(queue_, ^{
      registration_.reset();
      state_.store(stopped);
    });
  }

  DiscoveryStatus status() const noexcept
  {
    const auto value = state_.load();
    if (value == stopped)
      return {DiscoveryState::stopped, 0};
    if (value == registering)
      return {DiscoveryState::registering, 0};
    return value == kDNSServiceErr_NoError ? DiscoveryStatus{DiscoveryState::advertised, 0}
                                           : DiscoveryStatus{DiscoveryState::failed, value};
  }

private:
  static void registered(DNSServiceRef,
                         DNSServiceFlags,
                         DNSServiceErrorType error,
                         const char*,
                         const char*,
                         const char*,
                         void* context) noexcept
  {
    auto& self = *static_cast<Impl*>(context);
    self.state_.store(error);
    if (error != kDNSServiceErr_NoError)
      self.registration_.reset();
  }

  const std::uint32_t interface_index_;
  dispatch_queue_t queue_;
  std::unique_ptr<_DNSServiceRef_t, decltype(&DNSServiceRefDeallocate)> registration_{
      nullptr, DNSServiceRefDeallocate};
  std::atomic<std::int32_t> state_{stopped};
};

MacosDnsSd::MacosDnsSd(std::uint32_t interface_index)
    : impl_(std::make_unique<Impl>(interface_index))
{
}
MacosDnsSd::~MacosDnsSd() = default;
void MacosDnsSd::advertise(const ServiceRecord& record)
{
  impl_->advertise(record);
}
void MacosDnsSd::stop()
{
  impl_->stop();
}
DiscoveryStatus MacosDnsSd::status() const noexcept
{
  return impl_->status();
}
} // namespace studio_remote::macos
