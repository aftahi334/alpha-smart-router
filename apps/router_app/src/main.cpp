/**
 * @file router_app.cpp
 * @brief Top-level orchestrator for bootstrap, control/data planes, and RT wiring.
 * **Bootstrap**
 * Load config; construct ServiceRegistry, policies, selector/oracle; init RT (affinity, prio).
 * - Init memory primitives (PacketPool, SPSC).
 *
 * **Control plane (CP)**
 * - Apply/modify services; bind policies; ingest health/route; publish snapshots (RELEASE).
 *
 * **Data plane (DP)**
 * - Resolve service → candidates → policies → egress; allocation-free, no exceptions; SPSC 1P/1C.
 *
 * **Observability & lifecycle**
 * - Metrics and clean shutdown.
 *
 * **Invariants**
 * - RCU: readers ACQUIRE, writers RELEASE.
 * - SPSC: one producer, one consumer per ring.
 *
 * The routing/memory layers are validated via tests (test_mem, test_routing).
 * The actual router app wiring (I/O drivers, config plumbing, thread model)
 * isn’t implemented yet, so this binary just prints context and exits.
 */

#include <iostream>

int main() {
  std::cout
    << "alpha-smart-router: router_app placeholder\n"
    << "--------------------------------------------------\n"
    << "Router application wiring is not implemented yet.\n"
    << "What *is* validated right now:\n"
    << "  - Memory layer: SpscQueue & PacketPool (see: test_mem)\n"
    << "  - Routing layer: ServiceRegistry & policies (see: test_routing)\n"
    << "\n"
    << "Next steps before enabling a real app:\n"
    << "  1) Wire config -> ServiceRegistry (load/update) \n"
    << "  2) Bind policies / oracle into the live choose() fn\n"
    << "  3) Stand up I/O threads and SPSC rings (1P/1C)\n"
    << "  4) Add observability + clean shutdown\n"
    << std::endl;

  return 0;
}
