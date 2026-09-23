#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace studio_remote
{

// Fixed storage for one producer and one consumer. The producer owns a reserved write slot until
// commit_write, and the consumer owns the current read slot until commit_read.
template <typename Value, std::size_t Capacity> class FixedSpscRing
{
  static_assert(Capacity > 0);

public:
  Value* try_reserve_write() noexcept
  {
    const auto write = write_index_.load(std::memory_order_relaxed);
    const auto read = read_index_.load(std::memory_order_acquire);
    return write - read == Capacity ? nullptr : &values_[write % Capacity];
  }

  void commit_write() noexcept
  {
    write_index_.fetch_add(1, std::memory_order_release);
  }

  const Value* try_begin_read() const noexcept
  {
    const auto read = read_index_.load(std::memory_order_relaxed);
    return read == write_index_.load(std::memory_order_acquire) ? nullptr
                                                                : &values_[read % Capacity];
  }

  void commit_read() noexcept
  {
    read_index_.fetch_add(1, std::memory_order_release);
  }

  std::size_t size() const noexcept
  {
    return write_index_.load(std::memory_order_acquire) -
           read_index_.load(std::memory_order_acquire);
  }

private:
  std::array<Value, Capacity> values_{};
  std::atomic<std::uint32_t> write_index_{0};
  std::atomic<std::uint32_t> read_index_{0};
};

} // namespace studio_remote
