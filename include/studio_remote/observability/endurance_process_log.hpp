#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote
{
class EnduranceEventLog;

/// Process-scope owner, outside the service construction/failure scope. Start only after saving
/// configuration. All recorder borrowers must stop before finish/fail or this owner's destruction.
/// Lifecycle calls are serialized by the root; native bootstrap/teardown workers must be joined.
class EnduranceProcessLog final
{
public:
  EnduranceProcessLog();
  ~EnduranceProcessLog();
  EnduranceProcessLog(const EnduranceProcessLog&) = delete;
  EnduranceProcessLog& operator=(const EnduranceProcessLog&) = delete;

  EnduranceEventLog& start(const std::filesystem::path& directory, const std::string& version);
  void finish();
  void fail(std::string_view message);

private:
  std::unique_ptr<EnduranceEventLog> log_;
};
} // namespace studio_remote
