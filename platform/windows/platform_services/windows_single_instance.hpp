#pragma once

#include <Windows.h>

#include <string>

namespace studio_remote::windows
{

class WindowsSingleInstance
{
public:
  WindowsSingleInstance();
  explicit WindowsSingleInstance(std::wstring mutex_name);
  ~WindowsSingleInstance();

  WindowsSingleInstance(const WindowsSingleInstance&) = delete;
  WindowsSingleInstance& operator=(const WindowsSingleInstance&) = delete;

  [[nodiscard]] bool owns_instance() const;
  void signal_management_request() const;
  [[nodiscard]] bool consume_management_request() const;

private:
  HANDLE mutex_ = nullptr;
  HANDLE management_event_ = nullptr;
};

} // namespace studio_remote::windows
