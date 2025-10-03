/**
 * @file spsc_queue.hpp
 * @brief Single-producer/single-consumer ring buffer (owning, RT-friendly).
 *
 * Design goals:
 *  - Exception-free hot path (push/pop return bool).
 *  - One-time allocation during setup via factory; no allocations after.
 *  - Minimal synchronization: acquire/release pairs for SPSC.
 *  - Indices padded to avoid false sharing in RT workloads.
 *
 * Construction:
 *  - Use SpscQueue<T>::with_capacity(capacity_pow2) to build.
 *  - Constructor is noexcept and assumes validated inputs.
 *
 * @tparam T Element type. Must be trivially copyable or nothrow-movable.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "alpha/compat/expected.hpp"  // alpha_detail::expected / unexpected

namespace alpha::mem {

/// Cache line size hint (adjust per platform if needed).
inline constexpr std::size_t kCacheLine = 64;

/**
 * @brief Error codes reported by the factory (setup time only).
 * These errors are never produced during hot path operations.
 */
enum class SpscError : std::uint8_t {
  CapacityZero = 1,          ///< Capacity must not be zero
  CapacityNotPowerOfTwo,     ///< Capacity must be power-of-two
  AllocationFailed,          ///< Aligned allocation failed
  ElementNotNothrowMovable   ///< T must be trivially copyable or nothrow-movable
};

/// @brief Trait to constrain element types for RT-safety.
template <class T>
struct SpscTraits {
  static constexpr bool ok =
    std::is_trivially_copyable_v<T> ||
    std::is_nothrow_move_constructible_v<T>;
};

/**
 * @brief Single-producer, single-consumer ring buffer (owning).
 *
 * @tparam T Element type.
 */
template <class T>
class SpscQueue final {
  static_assert(std::atomic<std::size_t>::is_always_lock_free,
                "std::atomic<size_t> must be lock-free on this target");

public:
  using value_type = T;

  /// @brief Default-constructed empty shell (use with factory).
  SpscQueue() noexcept = default;

  /**
   * @brief Factory: validates input and allocates once (no exceptions).
   * @param capacity_pow2 Ring capacity (must be power-of-two, >= 2 recommended).
   * @return expected<SpscQueue, SpscError> constructed queue or error.
   */
  static alpha_detail::expected<SpscQueue, SpscError>
  with_capacity(std::size_t capacity_pow2) noexcept {
    if (capacity_pow2 == 0) {
      return alpha_detail::unexpected(SpscError::CapacityZero);
    }
    if ((capacity_pow2 & (capacity_pow2 - 1)) != 0) {
      return alpha_detail::unexpected(SpscError::CapacityNotPowerOfTwo);
    }
    if (!SpscTraits<T>::ok) {
      return alpha_detail::unexpected(SpscError::ElementNotNothrowMovable);
    }

    // Allocate storage once, aligned for T. We do not value-initialize elements.
    std::unique_ptr<T[], void(*)(T*)> storage(
      static_cast<T*>(::operator new[](capacity_pow2 * sizeof(T),
                                       std::align_val_t(alignof(T)))),
      [](T* p){ ::operator delete[](p, std::align_val_t(alignof(T))); });

    if (!storage) {
      return alpha_detail::unexpected(SpscError::AllocationFailed);
    }

    SpscQueue q;
    q.buf_      = storage.get();
    q.capacity_ = capacity_pow2;
    q.mask_     = capacity_pow2 - 1;
    q.storage_  = std::move(storage);
    return q;
  }

  SpscQueue(const SpscQueue&)            = delete; ///< Non-copyable
  SpscQueue& operator=(const SpscQueue&) = delete; ///< Non-assignable

  /// @brief Move constructor (avoid moving in-flight in RT).
  SpscQueue(SpscQueue&& other) noexcept { move_from(std::move(other)); }

  /// @brief Move assignment (avoid moving in-flight in RT).
  SpscQueue& operator=(SpscQueue&& other) noexcept {
    if (this != &other) move_from(std::move(other));
    return *this;
  }

  /**
   * @brief Push by const reference.
   * @param v Element to copy.
   * @return false if queue is full.
   */
  bool push(const T& v) noexcept {
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    const std::size_t n = (t + 1) & mask_;
    if (n == head_.load(std::memory_order_acquire)) {
      return false; // full
    }
    buf_[t] = v;
    tail_.store(n, std::memory_order_release);
    return true;
  }

  /**
   * @brief Push by rvalue reference.
   * @param v Element to move.
   * @return false if queue is full.
   */
  bool push(T&& v) noexcept(std::is_nothrow_move_constructible_v<T>) {
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    const std::size_t n = (t + 1) & mask_;
    if (n == head_.load(std::memory_order_acquire)) {
      return false; // full
    }
    buf_[t] = std::move(v);
    tail_.store(n, std::memory_order_release);
    return true;
  }

  /**
   * @brief Pop one element into output.
   * @param out Destination reference to receive the element.
   * @return false if queue is empty.
   */
  bool pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T> ||
                            std::is_trivially_copy_assignable_v<T>) {
    const std::size_t h = head_.load(std::memory_order_relaxed);
    if (h == tail_.load(std::memory_order_acquire)) {
      return false; // empty
    }
    out = std::move(buf_[h]);
    head_.store((h + 1) & mask_, std::memory_order_release);
    return true;
  }

  /// @brief True if queue is empty (observer).
  bool empty() const noexcept {
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
  }

  /// @brief True if queue is full (observer).
  bool full() const noexcept {
    const auto t = tail_.load(std::memory_order_acquire);
    return ((t + 1) & mask_) == head_.load(std::memory_order_acquire);
  }

  /// @brief Capacity (power-of-two).
  std::size_t capacity() const noexcept { return capacity_; }

  /// @brief Approximate size (not linearizable across threads).
  std::size_t approx_size() const noexcept {
    const auto t = tail_.load(std::memory_order_acquire);
    const auto h = head_.load(std::memory_order_acquire);
    return (t + capacity_ - h) & mask_;
  }

private:
  /// @brief Helper to implement noexcept move.
  void move_from(SpscQueue&& other) noexcept {
    head_.store(other.head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    tail_.store(other.tail_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    buf_       = other.buf_;
    capacity_  = other.capacity_;
    mask_      = other.mask_;
    storage_   = std::move(other.storage_);
    other.buf_ = nullptr;
    other.capacity_ = 0;
    other.mask_ = 0;
  }

  // Producer/consumer indices on separate cache lines (avoid false sharing)
  alignas(kCacheLine) std::atomic<std::size_t> head_{0}; ///< Consumer index
  alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; ///< Producer index

  // Read-mostly metadata and owning storage
  alignas(kCacheLine) T*                            buf_      = nullptr; ///< Non-owning view into storage_
  std::size_t                                       capacity_ = 0;      ///< Capacity (power-of-two)
  std::size_t                                       mask_     = 0;      ///< capacity_-1
  std::unique_ptr<T[], void(*)(T*)>                 storage_{nullptr, +[](T*){}}; ///< Owning storage
};

} // namespace alpha::mem
