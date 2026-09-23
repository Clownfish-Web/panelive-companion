#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace studio_remote::windows
{

struct StreamlabsInstallation
{
  std::filesystem::path directory;
  std::filesystem::path executable;
  std::string version;
  bool x64 = false;
  bool signature_trusted = false;
  bool signer_matches = false;
  bool supported = false;
  bool running = false;
};

std::vector<StreamlabsInstallation> detect_streamlabs_installations();
std::optional<StreamlabsInstallation> detect_supported_streamlabs_installation();
bool streamlabs_process_matches_installation(const StreamlabsInstallation& installation);

} // namespace studio_remote::windows
