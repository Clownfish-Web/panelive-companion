#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <source_location>
#include <studio_remote/common/types.hpp>

namespace studio_remote
{
/// Windows OBS diagnostic policy shared by native adapters. The adapter owns this object;
/// synchronous request layers may borrow it but must not retain it beyond the request.
/// Callbacks run on the emitting thread outside the callback mutex and must return promptly.
class ObsDiagnosticEvents final
{
public:
  using Handler = std::function<void(std::string_view, json)>;
  void on_event(Handler handler);
  void request_sent(std::string_view type,
                    std::string_view id,
                    const json& data,
                    std::source_location caller,
                    std::string_view reason,
                    std::uint64_t thread_id) const;
  void
  request_send_failed(std::string_view type, std::string_view id, std::uint64_t thread_id) const;
  void request_timed_out(std::string_view type,
                         std::string_view id,
                         std::uint64_t elapsed_ms,
                         std::uint64_t thread_id) const;
  void request_completed(std::string_view type,
                         std::string_view id,
                         std::uint64_t elapsed_ms,
                         std::uint64_t thread_id) const;
  void request_rejected(std::string_view type,
                        std::string_view id,
                        std::uint64_t elapsed_ms,
                        std::uint64_t thread_id) const;
  void event_received(std::string_view type, const json& data, std::uint64_t thread_id) const;

private:
  void emit(std::string_view event, json details) const;
  mutable std::mutex mutex_;
  Handler handler_;
};
} // namespace studio_remote
