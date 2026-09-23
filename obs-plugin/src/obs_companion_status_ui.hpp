#pragma once

#include "companion_capture_status.hpp"

#include <memory>

namespace studio_remote::obs_plugin
{

class ObsFrameCapture;

class ObsCompanionStatusUi
{
public:
  ObsCompanionStatusUi();
  ~ObsCompanionStatusUi();

  ObsCompanionStatusUi(const ObsCompanionStatusUi&) = delete;
  ObsCompanionStatusUi& operator=(const ObsCompanionStatusUi&) = delete;

  bool start(ObsFrameCapture& capture);
  void stop();
  void post_status(CompanionStatusSnapshot snapshot);

private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

} // namespace studio_remote::obs_plugin
