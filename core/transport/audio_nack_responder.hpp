#pragma once

#include <rtc/mediahandler.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>

namespace studio_remote
{

class AudioNackResponder final : public rtc::MediaHandler
{
public:
  static constexpr std::size_t retention_packets = 25;

  explicit AudioNackResponder(std::function<void(std::uint32_t retransmitted,
                                                 std::uint32_t deadline_missed,
                                                 std::uint32_t send_failed)> report);

  void incoming(rtc::message_vector& messages, const rtc::message_callback& send) override;
  void outgoing(rtc::message_vector& messages, const rtc::message_callback& send) override;

private:
  rtc::message_ptr find(std::uint16_t sequence_number) const;

  struct Entry
  {
    std::uint16_t sequence_number = 0;
    rtc::message_ptr packet;
  };

  mutable std::mutex mutex_;
  std::array<Entry, retention_packets> entries_{};
  std::size_t next_entry_ = 0;
  std::function<void(std::uint32_t, std::uint32_t, std::uint32_t)> report_;
};

} // namespace studio_remote
