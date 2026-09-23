#include "srt_sequential_stream.hpp"

#include <WS2tcpip.h>
#include <WinSock2.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <srt/srt.h>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{
namespace
{
constexpr int receive_timeout_ms = 250;
constexpr int connect_timeout_ms = 3'000;
constexpr std::size_t maximum_message_bytes = 64 * 1024;

template <typename Value>
bool set_socket_option(SRTSOCKET socket, SRT_SOCKOPT option, const Value& value)
{
  return srt_setsockopt(socket, 0, option, &value, sizeof(value)) != SRT_ERROR;
}

std::string last_srt_error(std::string_view operation)
{
  const auto* detail = srt_getlasterror_str();
  return std::string(operation) + (detail ? ": " + std::string(detail) : " failed");
}
} // namespace

struct SrtSequentialStream::Impl
{
  explicit Impl(SRTSOCKET connected_socket)
      : socket(connected_socket), message(maximum_message_bytes)
  {
  }

  SRTSOCKET socket = SRT_INVALID_SOCK;
  std::mutex socket_mutex;
  std::mutex read_mutex;
  std::atomic<bool> closing{false};
  std::vector<char> message;
  std::size_t message_offset = 0;
  std::size_t message_size = 0;
};

SrtSequentialStream::SrtSequentialStream(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation))
{
}

SrtSequentialStream::~SrtSequentialStream()
{
  cancel();
}

std::unique_ptr<SrtSequentialStream> SrtSequentialStream::connect(const std::string& host,
                                                                  std::uint16_t port,
                                                                  std::uint32_t latency_ms,
                                                                  std::string& error)
{
  if (host.empty() || port == 0 || latency_ms < 20 || latency_ms > 8'000)
  {
    error = "Invalid SRT endpoint";
    return {};
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* addresses = nullptr;
  const auto port_text = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &addresses) != 0)
  {
    error = "SRT host resolution failed";
    return {};
  }

  SRTSOCKET connected = SRT_INVALID_SOCK;
  for (auto* address = addresses; address; address = address->ai_next)
  {
    const auto candidate = srt_create_socket();
    if (candidate == SRT_INVALID_SOCK)
      continue;
    const auto transmission_type = SRTT_LIVE;
    const auto latency = static_cast<int>(latency_ms);
    if (!set_socket_option(candidate, SRTO_TRANSTYPE, transmission_type) ||
        !set_socket_option(candidate, SRTO_RCVLATENCY, latency) ||
        !set_socket_option(candidate, SRTO_PEERLATENCY, latency) ||
        !set_socket_option(candidate, SRTO_RCVTIMEO, receive_timeout_ms) ||
        !set_socket_option(candidate, SRTO_CONNTIMEO, connect_timeout_ms))
    {
      error = last_srt_error("SRT socket configuration");
      srt_close(candidate);
      continue;
    }
    if (srt_connect(candidate, address->ai_addr, static_cast<int>(address->ai_addrlen)) !=
        SRT_ERROR)
    {
      connected = candidate;
      break;
    }
    error = last_srt_error("SRT connect");
    srt_close(candidate);
  }
  freeaddrinfo(addresses);
  if (connected == SRT_INVALID_SOCK)
    return {};
  return std::unique_ptr<SrtSequentialStream>(
      new SrtSequentialStream(std::make_unique<Impl>(connected)));
}

int SrtSequentialStream::read(std::uint8_t* buffer, int bytes) noexcept
{
  if (!buffer || bytes <= 0)
    return -1;
  auto& state = *implementation_;
  std::lock_guard read_lock(state.read_mutex);
  while (!state.closing.load())
  {
    if (state.message_offset < state.message_size)
    {
      const auto available = state.message_size - state.message_offset;
      const auto copied = std::min<std::size_t>(static_cast<std::size_t>(bytes), available);
      std::memcpy(buffer, state.message.data() + state.message_offset, copied);
      state.message_offset += copied;
      return static_cast<int>(copied);
    }
    SRTSOCKET socket = SRT_INVALID_SOCK;
    {
      std::lock_guard socket_lock(state.socket_mutex);
      socket = state.socket;
    }
    if (socket == SRT_INVALID_SOCK)
      return 0;
    SRT_MSGCTRL control = srt_msgctrl_default;
    const auto received = srt_recvmsg2(
        socket, state.message.data(), static_cast<int>(state.message.size()), &control);
    if (received > 0)
    {
      state.message_offset = 0;
      state.message_size = static_cast<std::size_t>(received);
      continue;
    }
    if (received == SRT_ERROR)
    {
      int system_error = 0;
      const auto error = srt_getlasterror(&system_error);
      if (error == SRT_EASYNCRCV || error == SRT_ETIMEOUT)
        continue;
      return state.closing ? 0 : -1;
    }
  }
  return 0;
}

void SrtSequentialStream::cancel() noexcept
{
  auto& state = *implementation_;
  if (state.closing.exchange(true))
    return;
  SRTSOCKET socket = SRT_INVALID_SOCK;
  {
    std::lock_guard lock(state.socket_mutex);
    socket = state.socket;
    state.socket = SRT_INVALID_SOCK;
  }
  if (socket != SRT_INVALID_SOCK)
    srt_close(socket);
}

bool SrtSequentialStream::cancelled() const noexcept
{
  return implementation_->closing.load();
}

} // namespace studio_remote::windows
