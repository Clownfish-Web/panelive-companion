#pragma once

#include <studio_remote/media/source_media_inspector.hpp>

namespace studio_remote::macos
{
/// Synchronous native counterpart of WindowsSourceMediaInspector. Called by reconciliation,
/// outside state locks. Each call owns its asset; it never plays or activates the source.
class MacosSourceMediaInspector final : public ISourceMediaInspector
{
public:
  MacosSourceMediaInspector() = default;
  ~MacosSourceMediaInspector() override = default;
  MacosSourceMediaInspector(const MacosSourceMediaInspector&) = delete;
  MacosSourceMediaInspector& operator=(const MacosSourceMediaInspector&) = delete;

  SourceVisualCapability inspect(const LocalMediaSource& source) override;
};
} // namespace studio_remote::macos
