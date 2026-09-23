#include "macos_obs_connection.hpp"

#include "macos_obs_rpc_session.hpp"
#include "macos_obs_websocket.hpp"

#include <condition_variable>
#include <cstdlib>
#include <openssl/crypto.h>
#include <studio_remote/obs/obs_connection_callbacks.hpp>
#include <studio_remote/obs/obs_reconnect.hpp>
#include <syslog.h>
#include <thread>
#include <utility>

namespace studio_remote::macos
{
namespace
{
using namespace std::chrono_literals;

struct ConnectionSettings
{
  ConnectionSettings(std::string configured_host,
                     std::uint16_t configured_port,
                     std::string configured_password)
      : host(std::move(configured_host)), port(configured_port),
        password(std::move(configured_password))
  {
  }
  ~ConnectionSettings()
  {
    OPENSSL_cleanse(password.data(), password.size());
  }
  const std::string host;
  const std::uint16_t port;
  std::string password;
};
} // namespace

class ObsConnection::Impl
{
public:
  Impl(std::string host, std::uint16_t port, std::string password, SocketFactory sockets)
      : sockets_(std::move(sockets))
  {
    if (!sockets_)
      throw std::invalid_argument("OBS connection requires a socket factory");
    reconfigure(std::move(host), port, std::move(password));
  }
  ~Impl()
  {
    stop();
  }

  void start()
  {
    if (thread_.joinable())
      return;
    {
      std::lock_guard lock(mutex_);
      stopping_ = false;
    }
    thread_ = std::thread([this] { run(); });
  }
  void stop()
  {
    if (thread_.joinable() && thread_.get_id() == std::this_thread::get_id())
      throw std::logic_error("OBS callbacks cannot stop their reader");
    std::shared_ptr<ObsRpcSession> session;
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      connected_ = false;
      session = session_;
    }
    if (session)
      session->close();
    changed_.notify_all();
    if (thread_.joinable())
      thread_.join();
  }
  void reconfigure(std::string host, std::uint16_t port, std::string password)
  {
    if (host.empty() || host.size() > 253 || port == 0)
      throw std::invalid_argument("OBS WebSocket endpoint is invalid");
    auto settings =
        std::make_shared<ConnectionSettings>(std::move(host), port, std::move(password));
    std::shared_ptr<ObsRpcSession> session;
    {
      std::lock_guard lock(mutex_);
      settings_.swap(settings);
      ++revision_;
      connected_ = false;
      last_error_ = "OBS connection settings changed; reconnecting";
      session = session_;
    }
    if (session)
      session->close();
    changed_.notify_all();
  }
  bool connected() const
  {
    std::lock_guard lock(mutex_);
    return connected_;
  }
  std::string last_error() const
  {
    std::lock_guard lock(mutex_);
    return last_error_;
  }
  nlohmann::json request(std::string_view type,
                         nlohmann::json data,
                         const ObsDiagnosticEvents* diagnostics,
                         std::source_location caller,
                         std::string_view reason,
                         const ObsResponseMapping& map_response)
  {
    std::shared_ptr<ObsRpcSession> session;
    {
      std::lock_guard lock(mutex_);
      if (reader_id_ == std::this_thread::get_id())
        throw std::logic_error("OBS callbacks cannot issue synchronous requests");
      if (!connected_ || stopping_)
        throw std::runtime_error("OBS is not connected");
      session = session_;
    }
    return session->request(type, std::move(data), 8s, diagnostics, caller, reason, map_response);
  }
  void on_event(IObsAdapter::EventCallback callback)
  {
    callbacks_.on_event(std::move(callback));
  }
  void on_connection(IObsAdapter::ConnectionCallback callback)
  {
    callbacks_.on_connection(std::move(callback));
  }

private:
  bool current(std::uint64_t revision) const
  {
    std::lock_guard lock(mutex_);
    return !stopping_ && revision_ == revision;
  }
  bool set_error(std::string text)
  {
    std::lock_guard lock(mutex_);
    const bool changed = last_error_ != text;
    last_error_ = std::move(text);
    return changed;
  }
  void emit_connection(bool connected, std::string_view version)
  {
    if (const auto failure = callbacks_.connection_changed(connected, version))
      if (set_error(std::string(*failure)))
        syslog(LOG_ERR, "%s", std::string(*failure).c_str());
  }
  void emit_event(ObsWireEvent event)
  {
    if (const auto failure = callbacks_.event_received(
            {std::move(event.type), std::move(event.data), monotonic_ns()}))
      if (set_error(std::string(*failure)))
        syslog(LOG_ERR, "%s", std::string(*failure).c_str());
  }
  std::shared_ptr<ObsRpcSession> new_session(std::uint64_t revision)
  {
    std::shared_ptr<ObsRpcSession> session;
    try
    {
      session = std::make_shared<ObsRpcSession>(sockets_());
    }
    catch (...)
    {
      throw std::runtime_error("Could not create OBS WebSocket");
    }
    std::lock_guard lock(mutex_);
    if (stopping_ || revision_ != revision)
      return {};
    session_ = session;
    return session;
  }
  std::chrono::milliseconds attempt(const ConnectionSettings& settings, std::uint64_t revision)
  {
    const auto session = new_session(revision);
    if (!session)
      return 0ms;
    bool announced = false;
    std::optional<Clock::time_point> opened;
    try
    {
      const auto version = session->identify(settings.host, settings.port, settings.password);
      {
        std::lock_guard lock(mutex_);
        if (!stopping_ && revision_ == revision)
        {
          connected_ = true;
          announced = true;
          last_error_.clear();
        }
      }
      if (announced)
      {
        opened = Clock::now();
        emit_connection(true, version);
        receive_events(*session, revision);
      }
    }
    catch (const std::exception& error)
    {
      if (current(revision))
        set_error(error.what());
    }
    session->close();
    {
      std::lock_guard lock(mutex_);
      connected_ = false;
      session_.reset();
    }
    if (announced)
      emit_connection(false, {});
    return opened ? std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - *opened)
                  : 0ms;
  }
  void receive_events(ObsRpcSession& session, std::uint64_t revision)
  {
    while (current(revision))
    {
      auto received = session.read(5s);
      if (received.state == ObsSocketReadState::closed)
        return;
      if (received.event && current(revision))
        emit_event(std::move(*received.event));
    }
  }
  void reconnect_loop()
  {
    ObsReconnectBackoff backoff;
    while (true)
    {
      std::shared_ptr<const ConnectionSettings> settings;
      std::uint64_t revision;
      {
        std::lock_guard lock(mutex_);
        if (stopping_)
          return;
        settings = settings_;
        revision = revision_;
      }
      std::chrono::milliseconds age{0};
      try
      {
        age = attempt(*settings, revision);
      }
      catch (const std::exception& error)
      {
        if (current(revision))
          set_error(error.what());
      }
      const auto delay = backoff.next_delay(age, arc4random());
      std::unique_lock lock(mutex_);
      const bool interrupted =
          changed_.wait_for(lock, delay, [&] { return stopping_ || revision_ != revision; });
      if (interrupted)
        backoff.reset();
    }
  }
  void run() noexcept
  {
    {
      std::lock_guard lock(mutex_);
      reader_id_ = std::this_thread::get_id();
    }
    try
    {
      reconnect_loop();
    }
    catch (...)
    {
      set_error("Unexpected OBS connection worker failure");
      syslog(LOG_ERR, "Unexpected OBS connection worker failure");
    }
    std::shared_ptr<ObsRpcSession> session;
    {
      std::lock_guard lock(mutex_);
      connected_ = false;
      session = std::move(session_);
      reader_id_ = {};
    }
    if (session)
      session->close();
  }

  SocketFactory sockets_;
  // Protected by mutex_. Immutable settings are shared only with the current attempt.
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::shared_ptr<ConnectionSettings> settings_;
  std::shared_ptr<ObsRpcSession> session_;
  std::uint64_t revision_ = 0;
  bool stopping_ = true;
  bool connected_ = false;
  std::string last_error_;
  std::thread::id reader_id_;
  ObsConnectionCallbacks callbacks_;
  std::thread thread_;
};

ObsConnection::ObsConnection(std::string host, std::uint16_t port, std::string password)
    : ObsConnection(std::move(host),
                    port,
                    std::move(password),
                    [] { return std::make_unique<MacosObsWebSocket>(); })
{
}
ObsConnection::ObsConnection(std::string host,
                             std::uint16_t port,
                             std::string password,
                             SocketFactory sockets)
    : impl_(std::make_unique<Impl>(std::move(host), port, std::move(password), std::move(sockets)))
{
}
ObsConnection::~ObsConnection() = default;
void ObsConnection::start()
{
  impl_->start();
}
void ObsConnection::stop()
{
  impl_->stop();
}
void ObsConnection::reconfigure(std::string host, std::uint16_t port, std::string password)
{
  impl_->reconfigure(std::move(host), port, std::move(password));
}
bool ObsConnection::connected() const
{
  return impl_->connected();
}
std::string ObsConnection::last_error() const
{
  return impl_->last_error();
}
nlohmann::json ObsConnection::request(std::string_view type,
                                      nlohmann::json data,
                                      const ObsDiagnosticEvents* diagnostics,
                                      std::source_location caller,
                                      std::string_view reason,
                                      const ObsResponseMapping& map_response)
{
  return impl_->request(type, std::move(data), diagnostics, caller, reason, map_response);
}
void ObsConnection::on_event(IObsAdapter::EventCallback callback)
{
  impl_->on_event(std::move(callback));
}
void ObsConnection::on_connection(IObsAdapter::ConnectionCallback callback)
{
  impl_->on_connection(std::move(callback));
}
} // namespace studio_remote::macos
