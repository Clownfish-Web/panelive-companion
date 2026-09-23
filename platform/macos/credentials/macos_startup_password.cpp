#include "macos_startup_password.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <openssl/crypto.h>
#include <poll.h>
#include <stdexcept>
#include <syslog.h>
#include <system_error>
#include <termios.h>
#include <unistd.h>

namespace studio_remote::macos
{
namespace
{
class TerminalEcho final
{
public:
  explicit TerminalEcho(int descriptor) : descriptor_(descriptor)
  {
    if (!isatty(descriptor_))
    {
      if (errno != ENOTTY)
        throw std::system_error(errno, std::generic_category(), "Cannot inspect password input");
      return;
    }
    if (tcgetattr(descriptor_, &previous_) != 0)
      throw std::system_error(errno, std::generic_category(), "Cannot read terminal settings");
    auto hidden = previous_;
    hidden.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL));
    if (tcsetattr(descriptor_, TCSANOW, &hidden) != 0)
      throw std::system_error(errno, std::generic_category(), "Cannot disable terminal echo");
    changed_ = true;
  }
  ~TerminalEcho()
  {
    if (changed_ && tcsetattr(descriptor_, TCSANOW, &previous_) != 0)
      syslog(LOG_ERR, "Cannot restore terminal echo after password input");
  }
  void restore()
  {
    if (!changed_)
      return;
    if (tcsetattr(descriptor_, TCSANOW, &previous_) != 0)
      throw std::system_error(errno, std::generic_category(), "Cannot restore terminal echo");
    changed_ = false;
  }

private:
  const int descriptor_;
  termios previous_{};
  bool changed_ = false;
};

struct PasswordBuffer
{
  std::array<char, 4096> bytes{};
  std::size_t size = 0;
  ~PasswordBuffer()
  {
    OPENSSL_cleanse(bytes.data(), bytes.size());
  }
};

void check_cancelled(std::stop_token stop)
{
  if (stop.stop_requested())
    throw std::runtime_error("OBS password input was cancelled");
}

bool read_character(int descriptor,
                    char& character,
                    std::stop_token stop,
                    std::chrono::steady_clock::time_point deadline)
{
  for (;;)
  {
    check_cancelled(stop);
    const auto remaining = deadline - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero())
      throw std::runtime_error("OBS password input timed out");
    const auto wait = std::clamp<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count(), 1, 100);
    pollfd input{descriptor, POLLIN, 0};
    const int ready = poll(&input, 1, static_cast<int>(wait));
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready < 0 || (input.revents & POLLNVAL) != 0)
      throw std::runtime_error("Cannot wait for OBS password input");
    if (ready == 0)
      continue;
    const auto count = read(descriptor, &character, 1);
    if (count < 0 && (errno == EINTR || errno == EAGAIN))
      continue;
    if (count < 0)
      throw std::runtime_error("Cannot read OBS password input");
    return count == 1;
  }
}
} // namespace

void import_startup_obs_password(const std::function<void(std::string_view)>& persist,
                                 int descriptor,
                                 std::stop_token stop,
                                 std::chrono::milliseconds timeout)
{
  if (timeout < std::chrono::milliseconds::zero() || timeout > std::chrono::minutes(5))
    throw std::invalid_argument("Password input timeout must be between zero and five minutes");
  check_cancelled(stop);
  TerminalEcho terminal(descriptor);
  PasswordBuffer password;
  PasswordBuffer character;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (read_character(descriptor, character.bytes[0], stop, deadline))
  {
    if (character.bytes[0] == '\n')
      break;
    if (character.bytes[0] == '\0' || password.size == password.bytes.size())
      throw std::invalid_argument("OBS password input is invalid or exceeds 4096 bytes");
    password.bytes[password.size++] = character.bytes[0];
  }
  if (password.size == 0 && character.bytes[0] != '\n')
    throw std::runtime_error("No OBS password was supplied");
  if (password.size > 0 && password.bytes[password.size - 1] == '\r')
    --password.size;
  check_cancelled(stop);
  terminal.restore();
  check_cancelled(stop);
  persist(std::string_view(password.bytes.data(), password.size));
}
} // namespace studio_remote::macos
