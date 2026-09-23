#pragma once

#include <chrono>
#include <memory>

namespace studio_remote::macos
{
/// The composition root owns one instance, created before starting workers. One thread waits;
/// any live worker may request stop. Join all users before destruction. Signal dispositions
/// are process-wide and restored on destruction; no signal callback accesses mutable state.
class MacosShutdownSignal final
{
public:
  MacosShutdownSignal();
  ~MacosShutdownSignal();
  MacosShutdownSignal(const MacosShutdownSignal&) = delete;
  MacosShutdownSignal& operator=(const MacosShutdownSignal&) = delete;

  void wait();
  bool wait_for(std::chrono::milliseconds timeout);
  void request_stop();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
