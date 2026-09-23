#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace studio_remote::macos
{
/// Per-user process ownership and payload-free management activation. The default instance
/// scope is independent of configuration overrides. Custom scope is for isolated installations.
class MacosSingleInstance final
{
public:
  MacosSingleInstance();
  MacosSingleInstance(std::filesystem::path directory, std::string activation_name);
  ~MacosSingleInstance();
  MacosSingleInstance(const MacosSingleInstance&) = delete;
  MacosSingleInstance& operator=(const MacosSingleInstance&) = delete;

  bool owns_instance() const noexcept;
  void signal_management_request() const;
  bool consume_management_request() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
