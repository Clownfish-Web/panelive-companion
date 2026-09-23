#pragma once

#include <studio_remote/teleprompter/repository.hpp>

#include <nlohmann/json.hpp>

namespace studio_remote
{
struct TeleprompterResponse
{
  int status;
  nlohmann::json body;
};

[[nodiscard]] bool teleprompter_read_path(std::string_view path);
[[nodiscard]] bool teleprompter_progress_path(std::string_view path);
[[nodiscard]] TeleprompterResponse teleprompter_read(teleprompter::IRepository* repository,
                                                     std::string_view device_id,
                                                     std::string_view path);
[[nodiscard]] TeleprompterResponse teleprompter_report(teleprompter::IRepository* repository,
                                                       std::string_view device_id,
                                                       std::string_view path,
                                                       const nlohmann::json& body);
} // namespace studio_remote
