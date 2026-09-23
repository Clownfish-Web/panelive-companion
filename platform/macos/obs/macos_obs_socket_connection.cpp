#include "macos_obs_socket_connection.hpp"

#include <utility>

namespace studio_remote::macos
{
namespace
{
constexpr std::size_t maximum_message_bytes = 16 * 1024 * 1024;
constexpr std::size_t maximum_queued_messages = 64;
} // namespace

std::string ObsSocketConnection::error_text(Failure failure)
{
  switch (failure)
  {
  case Failure::none:
    return {};
  case Failure::transport:
    return "OBS WebSocket transport failed";
  case Failure::timeout:
    return "OBS WebSocket connection timed out";
  case Failure::binary_message:
    return "OBS WebSocket received a binary message";
  case Failure::receive_overflow:
    return "OBS WebSocket receive queue is full";
  case Failure::send_overflow:
    return "OBS WebSocket send queue is full";
  }
  return "OBS WebSocket transport failed";
}

ObsSocketConnection::ObsSocketConnection(std::chrono::milliseconds timeout)
{
  rtc::WebSocket::Configuration config;
  config.connectionTimeout = timeout;
  config.pingInterval = std::chrono::seconds(15);
  // The library default permits unlimited unanswered pings, hiding silent network loss.
  config.maxOutstandingPings = 2;
  config.maxMessageSize = maximum_message_bytes;
  socket_ = std::make_unique<rtc::WebSocket>(config);
}

ObsSocketConnection::~ObsSocketConnection()
{
  socket_->resetCallbacks();
  socket_->forceClose();
}

void ObsSocketConnection::install_callbacks(const std::shared_ptr<ObsSocketConnection>& owner)
{
  const std::weak_ptr<ObsSocketConnection> weak = owner;
  socket_->onOpen(
      [weak]
      {
        if (const auto state = weak.lock())
        {
          std::lock_guard lock(state->mutex_);
          if (state->phase_ == Phase::connecting)
            state->phase_ = Phase::open;
          state->changed_.notify_all();
        }
      });
  socket_->onClosed(
      [weak]
      {
        if (const auto state = weak.lock())
          state->remote_closed();
      });
  socket_->onError(
      [weak](std::string)
      {
        if (const auto state = weak.lock())
          state->fail(Failure::transport);
      });
  socket_->onMessage(
      [weak](rtc::message_variant value)
      {
        if (const auto state = weak.lock())
        {
          try
          {
            state->received(std::move(value));
          }
          catch (...)
          {
            state->fail(Failure::transport);
          }
        }
      });
}

bool ObsSocketConnection::open(const std::string& url, std::chrono::milliseconds timeout)
{
  try
  {
    // This operation mutex serializes socket API calls only. Callbacks never acquire it.
    std::lock_guard operation(socket_operation_mutex_);
    {
      std::lock_guard lock(mutex_);
      if (phase_ == Phase::closed)
        return false;
    }
    socket_->open(url);
  }
  catch (const std::exception&)
  {
    fail(Failure::transport);
  }
  bool opened = false;
  {
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, timeout, [this] { return phase_ != Phase::connecting; }))
      failure_ = Failure::timeout;
    opened = phase_ == Phase::open;
  }
  if (!opened)
    close();
  return opened;
}

bool ObsSocketConnection::send(std::string message)
{
  std::lock_guard operation(socket_operation_mutex_);
  {
    std::lock_guard lock(mutex_);
    if (phase_ != Phase::open)
      return false;
  }
  try
  {
    if (message.size() > maximum_message_bytes ||
        socket_->bufferedAmount() > maximum_message_bytes - message.size())
    {
      std::lock_guard lock(mutex_);
      failure_ = Failure::send_overflow;
      return false;
    }
    // libdatachannel returns false for accepted, buffered data. Only exceptions mean failure.
    static_cast<void>(socket_->send(std::move(message)));
    return true;
  }
  catch (const std::exception&)
  {
    fail(Failure::transport);
    return false;
  }
}

ObsSocketRead ObsSocketConnection::receive(std::chrono::milliseconds timeout)
{
  std::unique_lock lock(mutex_);
  if (!changed_.wait_for(
          lock, timeout, [this] { return phase_ == Phase::closed || !messages_.empty(); }))
    return {ObsSocketReadState::timeout, {}};
  if (messages_.empty())
    return {};
  auto result = std::move(messages_.front());
  messages_.pop_front();
  queued_bytes_ -= result.size();
  return {ObsSocketReadState::message, std::move(result)};
}

void ObsSocketConnection::close()
{
  fail(Failure::none);
  std::lock_guard operation(socket_operation_mutex_);
  socket_->resetCallbacks();
  socket_->forceClose();
}

std::string ObsSocketConnection::last_error() const
{
  std::lock_guard lock(mutex_);
  return error_text(failure_);
}

void ObsSocketConnection::received(rtc::message_variant value)
{
  std::lock_guard lock(mutex_);
  if (phase_ == Phase::closed)
    return;
  if (!std::holds_alternative<std::string>(value))
    failure_ = Failure::binary_message;
  else
  {
    auto text = std::get<std::string>(std::move(value));
    if (text.size() > maximum_message_bytes || messages_.size() >= maximum_queued_messages ||
        text.size() > maximum_message_bytes - queued_bytes_)
      failure_ = Failure::receive_overflow;
    else
    {
      queued_bytes_ += text.size();
      messages_.push_back(std::move(text));
      changed_.notify_all();
      return;
    }
  }
  phase_ = Phase::closed;
  messages_.clear();
  queued_bytes_ = 0;
  changed_.notify_all();
}

void ObsSocketConnection::remote_closed()
{
  std::lock_guard lock(mutex_);
  phase_ = Phase::closed;
  changed_.notify_all();
}

void ObsSocketConnection::fail(Failure failure)
{
  std::lock_guard lock(mutex_);
  phase_ = Phase::closed;
  if (failure_ == Failure::none)
    failure_ = failure;
  messages_.clear();
  queued_bytes_ = 0;
  changed_.notify_all();
}
} // namespace studio_remote::macos
