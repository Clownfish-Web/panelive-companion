#pragma once

#include <chrono>
#include <filesystem>

namespace studio_remote::linux
{
class ShutdownSignal
{
public:
  ShutdownSignal();
  ~ShutdownSignal();
  bool wait_for(std::chrono::milliseconds timeout);
  void request_stop();

private:
  int signal_descriptor_ = -1;
  int wake_descriptor_ = -1;
};

class SingleInstance
{
public:
  explicit SingleInstance(const std::filesystem::path& data_directory);
  ~SingleInstance();
  bool owns_instance() const noexcept;

private:
  int descriptor_ = -1;
  bool owns_instance_ = false;
};
} // namespace studio_remote::linux
