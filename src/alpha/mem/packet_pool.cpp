// =============================================================
// File: src/alpha/mem/packet_pool.cpp
// =============================================================
#include "alpha/mem/packet_pool.hpp"
#include <cassert>
#include <cstdlib>
#include <limits>

namespace alpha::mem {

/**
 * @brief Construct the packet pool and seed the free list with all handles.
 *
 * Uses SpscQueue<PacketHandle>::with_capacity(capacity_pow2 * 2) to create the
 * free list. Fails fast (abort) if construction preconditions are violated.
 */
PacketPool::PacketPool(std::size_t capacity_pow2)
  : capacity_(capacity_pow2)
{
  if (capacity_pow2 == 0 ||
      (capacity_pow2 & (capacity_pow2 - 1)) != 0 ||
      capacity_pow2 > (std::numeric_limits<std::size_t>::max() / 2) ||
      capacity_pow2 > static_cast<std::size_t>(std::numeric_limits<PacketHandle>::max())) {
    std::abort(); // RT bring-up: fail early if capacity is invalid.
  }

  // Build the SPSC ring via the factory.
  // NOTE: Our SPSC uses the one-slot-open scheme (max usable = capacity-1),
  // so we size the ring at 2x to hold all pool handles at once.
  auto ringExp = SpscQueue<PacketHandle>::with_capacity(capacity_pow2 * 2);
  if (!ringExp) {
    std::abort(); // RT bring-up: fail early if allocation failed.
  }
  free_ring_ = std::move(*ringExp);
  storage_.resize(capacity_pow2);

  // Seed the free list with all handles [0..capacity-1].
  for (std::size_t i = 0; i < capacity_; ++i) {
    const bool ok = free_ring_.push(static_cast<PacketHandle>(i));
    assert(ok && "PacketPool: seeding failed (ring too small?)");
  }
}

bool PacketPool::acquire(PacketHandle& out_handle) noexcept {
  return free_ring_.pop(out_handle);
}

bool PacketPool::release(PacketHandle handle) noexcept {
  return free_ring_.push(handle);
}

Packet& PacketPool::get(PacketHandle h) noexcept {
  assert(h < capacity_ && "PacketPool::get: handle out of range");
  return storage_[static_cast<std::size_t>(h)];
}

const Packet& PacketPool::get(PacketHandle h) const noexcept {
  assert(h < capacity_ && "PacketPool::get: handle out of range");
  return storage_[static_cast<std::size_t>(h)];
}

} // namespace alpha::mem