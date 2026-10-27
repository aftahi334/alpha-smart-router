# 📜 CHANGELOG — Alpha Smart Router

All notable changes to this project will be documented here.  
Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [0.2.0] – Routing Semantics & Concurrency Hardening (October 2026)

### Context
Second milestone release building on the `v0.1.0` baseline.  
Focus on hardening lock-free memory and seqlock primitives (`SpscQueue<T>`, `PacketPool`, `MetricsSlot`, `PolicyBinding`), separating Control Plane and Data Plane telemetry models, and completing deterministic ingress, QoS, and failover routing semantics.  
See [docs/verification/verification_plan.md](docs/verification/verification_plan.md) for validated test coverage.

### Added
- **Routing Core (`include/alpha/routing/`, `src/alpha/routing/`)**
  - Explicit Control Plane (`QoSPathMetrics`) ↔ Data Plane (`PathMetrics` / `DpPathMetrics`) telemetry conversion boundary and `jitter_us` tracking.
  - Deterministic `Flow5Tuple` and IPv4/IPv6 hashing (`HashSourceIP`, `Hash5Tuple`) in `IngressSelector`.
  - Explicit data-plane `RouteDecision` and `SelectReason` outcomes (`SelectedHealthy`, `DegradedFallback`, `NoCandidates`, `AllPathsUnhealthy`, `SnapshotUnavailable`).
  - DSCP-to-`QoSClass` classification (`classify_dscp`, `to_dp_qos_class`, `from_dp_qos_class`) wired into latency-aware path selection.
  - `ServiceRegistry::stats()` observability counters and `PopLike` template adapters.

- **Documentation & Diagrams (`docs/`)**
  - Added `cp_dp_concurrency.svg` and `routing_decision_flow.svg` (+ `.mmd` sources).
  - Updated `architecture.svg`, `packet_flow.svg`, and `verification_plan.md` to reflect CP/DP separation and packet handle lifecycle.

- **Unit Tests (`tests/`)**
  - `test_qos.cpp`: `QoSPolicy` scoring, DSCP classification, and CP↔DP metric conversions.
  - `test_ingress.cpp`: `RoundRobin`, `HashSourceIP`, `Hash5Tuple`, fallback handling, and `SimulatedBgpOracle`.
  - `test_failover.cpp`: primary `recovery_hold_ms` hysteresis, selection dwell (`min_hold_ms`), and degraded path policies.
  - Expanded `test_mem.cpp` and `test_routing.cpp` with object lifetime, bounds, concurrent seqlock, and RCU capacity tests (36 total test cases).

### Fixed
- **Memory Primitives (`include/alpha/mem/`, `src/alpha/mem/`)**
  - Explicit slot lifetime management (`std::construct_at` / `std::destroy_at`) and ring wrap-around cleanup in `SpscQueue<T>`.
  - Power-of-two capacity (`>= 2`), size overflow, and `std::nothrow` allocation guards in `SpscQueue<T>` and `PacketPool`.

- **Concurrency & Routing (`include/alpha/routing/`, `src/alpha/routing/`)**
  - Atomic seqlock publication with release/acquire fences in `MetricsSlot` and `PolicyBinding` (eliminating torn reads).
  - `ServiceRegistry` copy-on-write `upsertService` replacement, `Limits::MaxServices` capacity bound, and zero-allocation IP/PoP validators.
  - `FailoverPolicy` primary `recovery_hold_ms` enforcement across all candidate branches and separation of selection dwell from health transition timestamps.

---

## [0.1.0] – Baseline Architecture (October 2025)

### Context
Initial public baseline release after local incubation (Sept 2024 → Oct 2025).  
Focus on establishing a safe, test-driven foundation with memory primitives, concurrency mechanisms, and initial benchmarking.  
See [docs/verification/verification_plan.md](docs/verification/verification_plan.md) for details on validated foundations.

### Added
- **Application (`apps/`)**
  - `router_app`: lightweight scaffold that prints context.
    - Core logic validated via unit tests; pipeline integration planned for future releases.

- **Documentation & Diagrams (`docs/`)**
  - System blueprints: `architecture.svg`, `packet_flow.svg`, `memory_overview.svg` (+ `.mmd` sources).

- **Unit Tests (`tests/`)**
  - Memory primitives (`SpscQueue<T>`, `PacketPool`)
  - Concurrency (`ServiceRegistry` with RCU semantics + heterogeneous lookup)

- **Benchmarks (`benchmarks/`)**
  - `spec_bench`: baseline throughput for `push+pop` cycles on copyable and move-only types.  
