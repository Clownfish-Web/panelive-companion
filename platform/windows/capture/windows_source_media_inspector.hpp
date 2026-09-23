#pragma once

#include <studio_remote/media/source_media_inspector.hpp>

namespace studio_remote::windows
{

class WindowsSourceMediaInspector final : public ISourceMediaInspector
{
public:
  WindowsSourceMediaInspector();
  ~WindowsSourceMediaInspector() override;

  WindowsSourceMediaInspector(const WindowsSourceMediaInspector&) = delete;
  WindowsSourceMediaInspector& operator=(const WindowsSourceMediaInspector&) = delete;

  SourceVisualCapability inspect(const LocalMediaSource& source) override;

private:
  bool media_foundation_started_ = false;
};

} // namespace studio_remote::windows
