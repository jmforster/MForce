#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace mforce {

// A fixed-capacity queue between exactly one producer thread and one
// consumer thread. The items live in a ring inside the object; each side
// writes one index and only reads the other's, so neither call blocks,
// allocates or throws. The producer publishes an item by storing its write
// index with release order after writing the slot; the consumer reads that
// index with acquire order before reading the slot, and frees the slot the
// same way in the other direction.
template <class T, std::size_t N>
class SpscQueue {
  static_assert(std::is_trivially_copyable_v<T>, "a queue item is copied as bytes between threads");
  static_assert(N > 0, "a queue holds at least one item");

public:
  // Producer thread only. False when the queue holds N items.
  bool push(const T& item) noexcept {
    const std::size_t w = write_.load(std::memory_order_relaxed);
    if (w - read_.load(std::memory_order_acquire) == N) return false;
    slots_[w % N] = item;
    write_.store(w + 1, std::memory_order_release);
    return true;
  }

  // Consumer thread only. False when the queue is empty.
  bool pop(T& item) noexcept {
    const std::size_t r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire)) return false;
    item = slots_[r % N];
    read_.store(r + 1, std::memory_order_release);
    return true;
  }

  static constexpr std::size_t capacity() { return N; }

private:
  std::array<T, N> slots_{};
  // Counts of items ever written and read; their difference is the fill.
  std::atomic<std::size_t> write_{0};
  std::atomic<std::size_t> read_{0};
};

} // namespace mforce
