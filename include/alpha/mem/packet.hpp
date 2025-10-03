namespace alpha::mem {

/**
 * @file packet.hpp
 * @brief Lightweight packet descriptor and handle type for the Alpha Smart Router.
 *
 * The packet pool manages a fixed array of Packet objects and recycles them
 * using a lock-free SPSC free-list. The actual payload can be external
 * (e.g., DMA buffers) or embedded, depending on your application needs.
 * This default definition keeps the descriptor minimal and RT-friendly.
 */

/// @brief Index-based handle for addressing packets in the pool.
using PacketHandle = std::uint32_t;

/**
 * @brief Minimal packet descriptor.
 *
 * Keep this trivially movable/copyable so queue operations remain noexcept.
 * You can extend fields (timestamps, ports, metadata) as needed without
 * adding dynamic allocation.
 */
struct Packet final {
  /// @brief Current valid length of payload (bytes). Purely informational here.
  std::size_t length{0};

  /// @brief Optional small inline metadata (example). Adjust or remove freely.
  std::uint32_t meta{0};

  /// @brief Reserved for future use; keeps struct cache-friendly.
  std::uint32_t reserved{0};
};

} // namespace alpha::mem