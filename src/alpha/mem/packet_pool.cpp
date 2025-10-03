// =============================================================
// File: src/alpha/mem/packet_pool.cpp
// =============================================================
#include "alpha/mem/packet_pool.hpp"
#include <cassert>
#include <cstdlib>

namespace alpha::mem {

/**
 * @brief Construct the packet pool and seed the free list with all handles.
 *
 * Uses SpscQueue<PacketHandle>::with_capacity(capacity_pow2) to create the
 * free list. Fails fast (abort) if construction preconditions are violated.
 */
PacketPool::PacketPool(std::size_t capacity_pow2)
  : capacity_(capacity_pow2),
    storage_(capacity_pow2)  // default-construct Packet descriptors
{
  // Build the SPSC ring via the factory.
  // NOTE: Our SPSC uses the one-slot-open scheme (max usable = capacity-1),
  // so we size the ring at 2x to hold all pool handles at once.
  auto ringExp = SpscQueue<PacketHandle>::with_capacity(capacity_pow2 * 2);
  if (!ringExp) {
    std::abort(); // RT bring-up: fail early if capacity is invalid or allocation failed.
  }
  free_ring_ = std::move(*ringExp);

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