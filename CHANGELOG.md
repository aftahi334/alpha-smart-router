# 📜 CHANGELOG — Alpha Smart Router

All notable changes to this project will be documented here.  
Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

### Added
- **Routing Core (`alpha::routing`)**
  - Explicit Control Plane ↔ Data Plane metric conversion boundary (`loss_ratio_to_ppm`, `loss_ppm_to_ratio`, `to_dp_metrics`, `to_qos_metrics`) and `DpPathMetrics` alias.
  - `jitter_us` field on data-plane `PathMetrics` and lock-free `MetricsSlot`.
- **Unit Tests (`tests/`)**
  - `tests/test_qos.cpp`: unit tests for `QoSPolicy` scoring, DSCP classification, threshold violations, and CP↔DP metric conversions.
  - Concurrent 1-writer / multi-reader seqlock tests for `MetricsSlot` and publication tests for `PolicyBinding` in `tests/test_routing.cpp`.
  - Non-trivial object lifetime, ring wrap-around, move-assignment, and capacity/overflow validation tests for `SpscQueue<T>` in `tests/test_mem.cpp`.

### Changed
- **Routing Core (`alpha::routing`)**
  - Renamed the control-plane telemetry struct in `qos_policy.hpp` to `QoSPathMetrics` (`double loss` in `[0.0, 1.0]`) to eliminate the ODR/type collision with data-plane `PathMetrics` (`uint32_t loss_ppm` in `[0, 1'000'000]`).

### Fixed
- **Memory Layer (`alpha::mem`)**
  - Fixed `SpscQueue<T>` object lifetime management to construct elements in-place via `std::construct_at`, destroy popped elements via `std::destroy_at`, and destroy remaining queued elements on destruction or move-assignment.
  - Added capacity (`>= 2`, power-of-two), size multiplication overflow, and `std::nothrow` aligned allocation checks in `SpscQueue<T>::with_capacity` and `PacketPool`.
  - Made `include/alpha/mem/packet.hpp` self-contained with `#pragma once`, `<cstddef>`, and `<cstdint>`.
- **Concurrency & Seqlocks (`alpha::routing`)**
  - Replaced non-atomic `MetricsSlot` payload with lock-free `std::atomic` fields and added writer-entry `release` and reader-exit `acquire` fences to `MetricsSlot` and `PolicyBinding`.
  - Guarded path-selection policies against null `CandidateRef::slot` pointers and hardened `QoSPolicy::score_path` against negative or `NaN` packet loss inputs.

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
