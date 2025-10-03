/**
 * @file spsc_queue.cpp
 * @brief Explicit template instantiations for SpscQueue to reduce code bloat.
*/

#include "alpha/mem/spsc_queue.hpp"
#include "alpha/mem/packet.hpp"
namespace alpha::mem {

    /// Explicit instantiations of SpscQueue for commonly used types.
    /// This ensures one compiled instance instead of every TU instantiating its own.

    template class SpscQueue<int>;          // For unit tests and benchmarks
    template class SpscQueue<PacketHandle>; // for PacketHandle
} // namespace alpha::mem
