// =============================================================
// File: include/alpha/mem/packet_pool.hpp
// =============================================================
#pragma once

#include <cstddef>
#include <vector>

#include "alpha/mem/packet.hpp"
#include "alpha/mem/spsc_queue.hpp"

namespace alpha::mem {

/**
 * @file packet_pool.hpp
 * @brief Fixed-size pool of Packet objects with a lock-free SPSC free list.
 *
 * Design:
 *  - Capacity is fixed at construction and should be a power-of-two.
 *  - Free list implemented via SpscQueue<PacketHandle> (single producer/consumer).
 *  - Steady-state operations are allocation-free and exception-free.
 *
 * Thread roles (recommended):
 *  - RX thread: acquire() from free list to obtain handles for incoming packets.
 *  - TX thread: release() returned handles back to the free list after sending.
 */
class PacketPool {
public:
  /// @brief Construct a pool with @p capacity_pow2 packet descriptors.
  /// @param capacity_pow2 Pool capacity (must be power-of-two for the SPSC ring).
  explicit PacketPool(std::size_t capacity_pow2);

  PacketPool(const PacketPool&)            = delete;
  PacketPool& operator=(const PacketPool&) = delete;
  PacketPool(PacketPool&&)                 = default;
  PacketPool& operator=(PacketPool&&)      = default;

  /// @brief Try to acquire a free packet handle from the pool.
  /// @param out_handle Set to a valid handle on success.
  /// @return false if the free list is empty.
  bool acquire(PacketHandle& out_handle) noexcept;

  /// @brief Return a handle back to the pool.
  /// @param handle The handle to recycle.
  /// @return false if the free list is full (logic error in typical usage).
  bool release(PacketHandle handle) noexcept;

  /// @brief Access the packet descriptor by handle (no bounds checks in Release).
  /// @param h Valid packet handle in range [0, capacity()).
  /// @return Reference to the packet descriptor.
  Packet&       get(PacketHandle h)       noexcept;
  const Packet& get(PacketHandle h) const noexcept;

  /// @brief Pool capacity (number of Packet descriptors).
  std::size_t capacity() const noexcept { return capacity_; }

private:
  std::size_t                 capacity_{0};
  std::vector<Packet>         storage_{};            ///< Backing storage for descriptors
  SpscQueue<PacketHandle>     free_ring_{};          ///< SPSC free-list of handles
};

} // namespace alpha::mem