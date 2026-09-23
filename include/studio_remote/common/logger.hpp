#pragma once
#include <map>
#include <string>
#include <string_view>
namespace studio_remote
{
enum class LogLevel
{
  debug,
  info,
  warning,
  error
};
class ILogger
{
public:
  virtual ~ILogger() = default;
  virtual void write(LogLevel, std::string_view, const std::map<std::string, std::string>&) = 0;
};
} // namespace studio_remote
