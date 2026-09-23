#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace studio_remote::windows
{

/// Sequential receiver backed by one connected SRT caller socket.
class SrtSequentialStream
{
public:
  static std::unique_ptr<SrtSequentialStream> connect(const std::string& host,
                                                      std::uint16_t port,
                                                      std::uint32_t latency_ms,
                                                      std::string& error);

  ~SrtSequentialStream();

  SrtSequentialStream(const SrtSequentialStream&) = delete;
  SrtSequentialStream& operator=(const SrtSequentialStream&) = delete;

  /// Returns bytes read, zero after cancellation, or -1 after a transport failure.
  int read(std::uint8_t* buffer, int bytes) noexcept;
  void cancel() noexcept;
  bool cancelled() const noexcept;

private:
  struct Impl;
  explicit SrtSequentialStream(std::unique_ptr<Impl> implementation);

  std::unique_ptr<Impl> implementation_;
};

} // namespace studio_remote::windows
