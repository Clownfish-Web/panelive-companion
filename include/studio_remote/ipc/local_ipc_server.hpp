#pragma once
#include <functional>
#include <string>
#include <string_view>
namespace studio_remote
{
class ILocalIpcServer
{
public:
  using Handler = std::function<std::string(std::string_view)>;
  virtual ~ILocalIpcServer() = default;
  virtual void start(Handler) = 0;
  virtual void stop() = 0;
};
} // namespace studio_remote
