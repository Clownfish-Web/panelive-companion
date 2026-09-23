#include "linux_network.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>
#include <ifaddrs.h>
#include <mutex>
#include <net/if.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace studio_remote::linux
{
namespace
{
bool private_ipv4(std::uint32_t address)
{
  const auto host = ntohl(address);
  return (host & 0xff000000U) == 0x0a000000U || (host & 0xfff00000U) == 0xac100000U ||
         (host & 0xffff0000U) == 0xc0a80000U || (host & 0xffff0000U) == 0xa9fe0000U;
}
} // namespace

std::vector<std::string> available_lan_ipv4_addresses()
{
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0)
    throw std::runtime_error("Could not enumerate Linux network interfaces");
  std::vector<std::string> result;
  for (auto* entry = interfaces; entry; entry = entry->ifa_next)
  {
    if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || !(entry->ifa_flags & IFF_UP) ||
        (entry->ifa_flags & IFF_LOOPBACK))
      continue;
    const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
    if (!private_ipv4(address->sin_addr.s_addr))
      continue;
    char text[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)))
      result.emplace_back(text);
  }
  freeifaddrs(interfaces);
  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::string listener_specification(std::uint16_t port, bool allow_non_private_interfaces)
{
  if (allow_non_private_interfaces)
    return std::to_string(port) + "s";
  auto listeners = std::string("127.0.0.1:") + std::to_string(port) + "s";
  for (const auto& address : available_lan_ipv4_addresses())
    listeners += "," + address + ":" + std::to_string(port) + "s";
  return listeners;
}

bool listener_port_available(std::uint16_t port)
{
  const auto socket_descriptor = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (socket_descriptor < 0)
    throw std::runtime_error("Could not create listener diagnostic socket");
  const int enabled = 1;
  setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  const auto available =
      ::bind(socket_descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
  ::close(socket_descriptor);
  return available;
}

class AvahiServiceDiscovery::Implementation
{
public:
  ~Implementation()
  {
    stop();
  }

  void advertise(const ServiceRecord& record)
  {
    stop();
    int error = 0;
    poll_ = avahi_simple_poll_new();
    if (!poll_)
      throw std::runtime_error("Could not create Avahi event loop");
    client_ = avahi_client_new(
        avahi_simple_poll_get(poll_), AVAHI_CLIENT_NO_FAIL, nullptr, nullptr, &error);
    if (!client_)
      throw std::runtime_error("Could not connect to Avahi: " + std::string(avahi_strerror(error)));
    group_ = avahi_entry_group_new(client_, nullptr, nullptr);
    if (!group_)
      throw std::runtime_error("Could not create Avahi service group");
    AvahiStringList* text = nullptr;
    for (const auto& [key, value] : record.txt)
      text = avahi_string_list_add_pair(text, key.c_str(), value.c_str());
    error = avahi_entry_group_add_service_strlst(group_,
                                                 AVAHI_IF_UNSPEC,
                                                 AVAHI_PROTO_UNSPEC,
                                                 static_cast<AvahiPublishFlags>(0),
                                                 record.instance_name.c_str(),
                                                 record.service_type.c_str(),
                                                 nullptr,
                                                 nullptr,
                                                 record.port,
                                                 text);
    avahi_string_list_free(text);
    if (error >= 0)
      error = avahi_entry_group_commit(group_);
    if (error < 0)
      throw std::runtime_error("Could not publish Avahi service: " +
                               std::string(avahi_strerror(error)));
    thread_ = std::thread([this] { avahi_simple_poll_loop(poll_); });
  }

  void stop()
  {
    if (poll_)
      avahi_simple_poll_quit(poll_);
    if (thread_.joinable())
      thread_.join();
    if (group_)
      avahi_entry_group_free(group_);
    if (client_)
      avahi_client_free(client_);
    if (poll_)
      avahi_simple_poll_free(poll_);
    group_ = nullptr;
    client_ = nullptr;
    poll_ = nullptr;
  }

private:
  AvahiSimplePoll* poll_ = nullptr;
  AvahiClient* client_ = nullptr;
  AvahiEntryGroup* group_ = nullptr;
  std::thread thread_;
};

AvahiServiceDiscovery::AvahiServiceDiscovery() : implementation_(std::make_unique<Implementation>())
{
}

AvahiServiceDiscovery::~AvahiServiceDiscovery() = default;

void AvahiServiceDiscovery::advertise(const ServiceRecord& record)
{
  implementation_->advertise(record);
}

void AvahiServiceDiscovery::stop()
{
  implementation_->stop();
}
} // namespace studio_remote::linux
