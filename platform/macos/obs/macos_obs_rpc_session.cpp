#include "macos_obs_rpc_session.hpp"

#include <pthread.h>
#include <stdexcept>
#include <studio_remote/obs/obs_rpc_request_exchange.hpp>
#include <syslog.h>
#include <utility>
#include <uuid/uuid.h>

namespace studio_remote::macos
{
using namespace std::chrono_literals;
using json = nlohmann::json;

ObsRpcSession::ObsRpcSession(std::unique_ptr<IObsWebSocket> socket) : socket_(std::move(socket))
{
  if (!socket_)
    throw std::invalid_argument("OBS session requires a socket");
}
ObsRpcSession::~ObsRpcSession()
{
  close();
}

std::string ObsRpcSession::handshake_message(std::string_view failure)
{
  const auto message = socket_->receive(5s);
  if (message.state != ObsSocketReadState::message)
    throw std::runtime_error(std::string(failure));
  return message.message;
}

std::string
ObsRpcSession::identify(std::string_view host, std::uint16_t port, std::string_view password)
{
  {
    std::lock_guard lock(mutex_);
    if (phase_ != Phase::initial)
      throw std::runtime_error("OBS session is no longer available");
    phase_ = Phase::identifying;
  }
  try
  {
    if (!socket_->connect(host, port, 5s))
      throw std::runtime_error("Could not connect to OBS");
    {
      std::lock_guard lock(mutex_);
      if (phase_ == Phase::closed)
        throw std::runtime_error("OBS connection lost");
    }
    const auto hello = parse_obs_hello(handshake_message("OBS closed before Hello"), password);
    if (!socket_->send(hello.identify_message))
      throw std::runtime_error("Could not send OBS Identify");
    validate_obs_identified(handshake_message("OBS rejected or closed during Identify"));
    std::lock_guard lock(mutex_);
    if (phase_ == Phase::closed)
      throw std::runtime_error("OBS connection lost");
    phase_ = Phase::ready;
    return hello.version;
  }
  catch (...)
  {
    close();
    throw;
  }
}

ObsSessionRead ObsRpcSession::read(std::chrono::milliseconds timeout)
{
  try
  {
    const auto received = socket_->receive(timeout);
    if (received.state == ObsSocketReadState::closed)
      close();
    if (received.state != ObsSocketReadState::message)
      return {received.state, {}};
    auto message = parse_obs_message(received.message, pending_);
    if (auto* event = std::get_if<ObsWireEvent>(&message))
      return {ObsSocketReadState::message, std::move(*event)};
    if (auto* response = std::get_if<ObsWireResponse>(&message))
      complete(std::move(*response));
    return {ObsSocketReadState::message, {}};
  }
  catch (...)
  {
    close();
    throw;
  }
}

json ObsRpcSession::request(std::string_view type,
                            json data,
                            std::chrono::milliseconds timeout,
                            const ObsDiagnosticEvents* diagnostics,
                            std::source_location caller,
                            std::string_view reason,
                            const ObsResponseMapping& map_response)
{
  if (timeout < 0ms || timeout > 8s)
    throw std::invalid_argument("OBS request deadline is invalid");
  {
    std::lock_guard lock(mutex_);
    if (phase_ != Phase::ready)
      throw std::runtime_error("OBS is not connected");
    if (pending_.full())
      throw std::runtime_error("OBS request queue is full");
  }
  uuid_t bytes;
  uuid_generate_random(bytes);
  uuid_string_t id;
  uuid_unparse_lower(bytes, id);
  const auto started = monotonic_ns();
  std::uint64_t thread_id = 0;
  if (pthread_threadid_np(nullptr, &thread_id) != 0)
    syslog(LOG_ERR, "Could not read OBS diagnostic thread ID");
  const ObsRpcRequestOperations operations{
      [this](std::string_view request_id) { return register_request(request_id); },
      [this](std::string_view request_type, std::string_view request_id, json request_data)
      {
        return socket_->send(make_obs_request(request_type, request_id, std::move(request_data)));
      },
      [started] { return (monotonic_ns() - started) / 1'000'000ULL; }};
  return exchange_obs_rpc_request(pending_,
                                  {type, id, std::move(data), timeout, caller, reason, thread_id},
                                  operations,
                                  diagnostics,
                                  map_response);
}

std::future<json> ObsRpcSession::register_request(std::string_view id)
{
  std::lock_guard lock(mutex_);
  if (phase_ != Phase::ready)
    throw std::runtime_error("OBS is not connected");
  return pending_.register_request(std::string(id));
}

void ObsRpcSession::complete(ObsWireResponse response)
{
  std::lock_guard lock(mutex_);
  if (phase_ != Phase::ready)
    return;
  complete_obs_response(pending_, std::move(response));
}

void ObsRpcSession::close()
{
  const auto failure = std::make_exception_ptr(std::runtime_error("OBS connection lost"));
  {
    std::lock_guard lock(mutex_);
    phase_ = Phase::closed;
    pending_.cancel_all(failure);
  }
  socket_->close();
}
} // namespace studio_remote::macos
